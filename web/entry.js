// ---------------------------------------------------------------- entry.js
//
// 页面与 wasm 之间的唯一胶水层，pie / sparkbar / table 三类组件共用一份。
// 构建时用 --post-js 拼进 kiriyama_canvas.js，运行在 Module 的作用域里，
// 因此这里能直接用 ccall 这类运行时方法，页面则完全不需要知道
// 编码格式、字符串指针、导出符号名。
//
// 实例模型：一个 wasm 装组件类型，想画几个就创建几个实例，句柄就是身份。
//
//   var a = Module.createComponent('pie');
//   var b = Module.createComponent('pie');          // 第二个饼图，互不干扰
//   Module.loadPie(a, modelA);
//   Module.loadPie(b, modelB);
//   Module.showComponent(a, { x: 0,   y: 0, width: 470, height: 540 });
//   Module.showComponent(b, { x: 490, y: 0, width: 470, height: 540 });
//
//   // 一步到位
//   var c = Module.mount('sparkbar', { x: 0, y: 0, width: 960, height: 540 }, model);
//
// 属性一律用 Module['x'] 访问：--closure=1 会重命名点号属性。
//
// ⚠️ 同理，**页面传进来的模型对象**的属性也必须用 quoted 下标读
// （model['columns']、model['slices']……）：--closure=1 会把 entry.js 里的
// 点号属性统一重命名（slices → Si、sortable → xj 都实际发生过），而页面里的
// 对象字面量不经过 closure，属性名保持原文——点号读出去就是 undefined。

(function () {
  // 强制 2x 超采样：SDL_RenderGeometry 没有 MSAA，DPR=1 的屏幕上多边形边缘
  // （饼图扇形 / 圆）是 1 像素硬切。把 devicePixelRatio 钉在 2（高分屏不升也不降，
  // 省内存），SDL 的 HIGH_PIXEL_DENSITY 路径用它算 canvas 物理分辨率，
  // 边缘采样密度翻倍。必须在 wasm 初始化（创建 GL context）之前生效——
  // entry.js 是 --post-js，先于 runtime init 执行，位置正确。
  try {
    Object.defineProperty(window, 'devicePixelRatio', { value: 2, configurable: false });
  } catch (e) { /* 某些内嵌环境不允许覆盖，退回真实 DPR */ }

  // 行协议，见 c_api.cc 的 parseTableModel：
  //   C␟id␟title␟type␟width␟sortable␟format     列定义
  //   R␟key␟cell1␟cell2␟...                     一行
  // U+001F 在日常文本里不会出现，所以不需要转义。
  var UNIT = '\u001f';

  function call(name, argTypes, args) {
    return Module['ccall'](name, null, argTypes, args);
  }

  /// 句柄参数统一走这里：所有 kiriyama_<组件>_* 的第一个参数都是句柄。
  function withHandle(handle, name, argTypes, args) {
    return call(name, ['number'].concat(argTypes), [handle].concat(args));
  }

  // ================================================================ 实例

  /// 组件类型表。字符串数组而不是对象字面量：--closure=1 会重命名对象字面量
  /// 的无引号键，按字符串动态查表的老写法会查不到。
  var COMPONENT_TYPES = ['pie', 'sparkbar', 'table', 'line'];

  function componentKey(type) {
    var key = String(type).toLowerCase();
    if (COMPONENT_TYPES.indexOf(key) < 0) {
      throw new Error('未知组件 "' + type + '"（pie / sparkbar / table / line）');
    }
    return key;
  }

  /// 创建一个实例，返回句柄（>= 1；失败为 0）。
  Module['createComponent'] = function (type) {
    return Module['ccall']('kiriyama_create_component', 'number', ['string'],
                           [componentKey(type)]);
  };

  Module['destroyComponent'] = function (handle) {
    call('kiriyama_destroy_component', ['number'], [handle | 0]);
  };

  /// 销毁全部实例（画布清空）。
  Module['clearComponents'] = function () {
    call('kiriyama_clear_components', [], []);
  };

  Module['componentCount'] = function () {
    return Module['ccall']('kiriyama_component_count', 'number', [], []);
  };

  /// showComponent(handle, { x, y, width, height })：逻辑像素坐标。
  /// 同一个句柄再调用就是改位置与大小。
  Module['showComponent'] = function (handle, frame) {
    if (!frame) {
      throw new Error('showComponent: 需要 { x, y, width, height }');
    }
    withHandle(handle | 0, 'kiriyama_show_component',
               ['number', 'number', 'number', 'number'],
               [Number(frame['x'] || 0), Number(frame['y'] || 0),
                Number(frame['width']), Number(frame['height'])]);
  };

  Module['hideComponent'] = function (handle) {
    withHandle(handle | 0, 'kiriyama_hide_component', [], []);
  };

  Module['componentVisible'] = function (handle) {
    return withHandle(handle | 0, 'kiriyama_component_visible', [], []) === 1;
  };

  // ================================================================ 主题

  /// 页面级主题："light" / "dark"。描述树只发语义 token，颜色在渲染层
  /// 按当前主题解析——切主题不重建任何实例 / 数据，下一帧直接生效。
  Module['setTheme'] = function (name) {
    call('kiriyama_set_theme', ['string'], [String(name || '')]);
  };

  // ================================================================ 表格

  function cellText(value) {
    return value === null || value === undefined ? '' : String(value);
  }

  function encodeTable(model) {
    var columns = model['columns'] || [];
    var rows = model['rows'] || [];
    var lines = [];

    for (var c = 0; c < columns.length; c++) {
      var col = columns[c];
      lines.push([
        'C',
        col['id'],
        col['title'] == null ? col['id'] : col['title'],
        col['type'] || 'text',
        col['width'] == null ? 120 : col['width'],
        col['sortable'] === false ? 0 : 1,
        col['format'] || ''
      ].join(UNIT));
    }

    for (var r = 0; r < rows.length; r++) {
      var row = rows[r];
      var cells = row['cells'] || [];
      var fields = ['R', row['key'] == null ? cellText(cells[0]) : row['key']];
      for (var i = 0; i < columns.length; i++) {
        fields.push(cellText(cells[i]));
      }
      lines.push(fields.join(UNIT));
    }

    return lines.join('\n');
  }

  /// loadTable(handle, model)：对象或 JSON 文本都行。
  Module['loadTable'] = function (handle, model) {
    if (typeof model === 'string') {
      model = JSON.parse(model);
    }
    if (!model || !model['columns']) {
      throw new Error('loadTable: 缺少 model.columns');
    }
    withHandle(handle | 0, 'kiriyama_table_load', ['string'], [encodeTable(model)]);
    return true;
  };

  // ================================================================ 饼图

  /// 颜色：'#RRGGBB' → 0xRRGGBB，'none' / 缺失 → -1（C 侧负值 = 全透明）。
  function colorValue(value) {
    if (value === 'none' || value === 'transparent' || value == null) {
      return -1;
    }
    var text = String(value);
    if (text.charAt(0) === '#') {
      text = text.substring(1);
    }
    var rgb = parseInt(text, 16);
    return isNaN(rgb) ? -1 : rgb;
  }

  /// 气泡贴哪一侧：字符串 above/below/left/right（也接受数字）。
  function tooltipSideIndex(value) {
    if (typeof value === 'number') {
      return value;
    }
    var text = String(value).toLowerCase();
    if (text === 'below' || text === 'bottom') { return 1; }
    if (text === 'left') { return 2; }
    if (text === 'right') { return 3; }
    return 0;  // above / top / 未知
  }

  /// 每个 setter 都是 (handle, value)：样式跟着实例走，不是全局。
  ///
  /// ⚠️ 用 [name, fn] 数组而不是对象字面量：--closure=1 会重命名对象字面量里
  /// 未加引号的键（pie → Si 这种），而查表的 key 来自页面（不经过 closure，
  /// 保持原文），按名查表会查不到——结果是 options 被静默丢弃，图照样画出来，
  /// 只是样式全没生效，极难发现。字符串字面量不会被重命名，所以存成数组。
  var PIE_OPTION_SETTERS = [
    ['labelFontSize', function (h, v) { withHandle(h, 'kiriyama_pie_set_label_font_size', ['number'], [Number(v)]); }],
    ['labelColor',    function (h, v) { withHandle(h, 'kiriyama_pie_set_label_color', ['number'], [colorValue(v)]); }],
    ['showLabels',    function (h, v) { withHandle(h, 'kiriyama_pie_set_show_labels', ['number'], [v ? 1 : 0]); }],
    ['showValues',    function (h, v) { withHandle(h, 'kiriyama_pie_set_show_values', ['number'], [v ? 1 : 0]); }],
    ['gapWidth',      function (h, v) { withHandle(h, 'kiriyama_pie_set_gap_width', ['number'], [Number(v)]); }],
    ['gapColor',      function (h, v) { withHandle(h, 'kiriyama_pie_set_gap_color', ['number'], [colorValue(v)]); }],
    ['explode',       function (h, v) { withHandle(h, 'kiriyama_pie_set_explode', ['number'], [Number(v)]); }],
    ['innerRadius',   function (h, v) { withHandle(h, 'kiriyama_pie_set_inner_radius', ['number'], [Number(v)]); }],
    ['startAngle',    function (h, v) { withHandle(h, 'kiriyama_pie_set_start_angle', ['number'], [Number(v)]); }],
    ['labelOffset',   function (h, v) { withHandle(h, 'kiriyama_pie_set_label_offset', ['number'], [Number(v)]); }],
    // 气泡：箭头宽度（0 = 不画那个倒三角，默认 0）、字号（正文自动小 1px）
    ['tooltipArrowWidth', function (h, v) { withHandle(h, 'kiriyama_tooltip_set_arrow_width', ['number'], [Number(v)]); }],
    ['tooltipFontSize',   function (h, v) { withHandle(h, 'kiriyama_tooltip_set_font_size', ['number'], [Number(v)]); }],
    // 贴哪一侧：'above'（默认，浮在图上方）/ 'below' / 'left' / 'right'
    ['tooltipSide',       function (h, v) { withHandle(h, 'kiriyama_tooltip_set_side', ['number'], [tooltipSideIndex(v)]); }],
    // 跟随鼠标（默认 true）：气泡像浮层一样跟着光标走
    ['tooltipFollowPointer', function (h, v) { withHandle(h, 'kiriyama_tooltip_set_follow_pointer', ['number'], [v ? 1 : 0]); }]
  ];

  /// 按 name 线性查表（表就十几项，比踩 closure 重命名的坑划算）。
  function applyOptions(handle, options, table) {
    if (!options || typeof options !== 'object') {
      return;
    }
    for (var key in options) {
      if (!Object.prototype.hasOwnProperty.call(options, key)) {
        continue;
      }
      for (var i = 0; i < table.length; i++) {
        if (table[i][0] === key) {
          table[i][1](handle, options[key]);
          break;
        }
      }
    }
  }

  /// loadPie(handle, model)
  Module['loadPie'] = function (handle, model) {
    if (typeof model === 'string') {
      model = JSON.parse(model);
    }
    if (!model || !model['slices']) {
      throw new Error('loadPie: 缺少 model.slices');
    }
    var h = handle | 0;
    applyOptions(h, model['options'], PIE_OPTION_SETTERS);

    var slices = model['slices'];
    withHandle(h, 'kiriyama_pie_begin_slices', [], []);
    for (var i = 0; i < slices.length; i++) {
      var slice = slices[i];
      var label = slice['label'] == null ? '' : String(slice['label']);
      var value = Number(slice['value']);
      withHandle(h, 'kiriyama_pie_add_slice', ['string', 'number'],
                 [label, isNaN(value) ? NaN : value]);
    }
    withHandle(h, 'kiriyama_pie_commit', [], []);
    return true;
  };

  /// 当前 hover 的扇区下标（-1 = 没有）。legend 联动之类用。
  Module['pieHovered'] = function (handle) {
    return withHandle(handle | 0, 'kiriyama_pie_hovered', [], []);
  };

  /// 可用扇区数（0 / 负 / NaN 片不算）。
  Module['pieSliceCount'] = function (handle) {
    return withHandle(handle | 0, 'kiriyama_pie_slice_count', [], []);
  };

  // ================================================================ SparkStackBar

  var SPARK_OPTION_SETTERS = [
    ['barWidth',     function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_bar_width', ['number'], [Number(v)]); }],
    ['barGap',       function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_bar_gap', ['number'], [Number(v)]); }],
    ['segmentGap',   function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_segment_gap', ['number'], [Number(v)]); }],
    ['cornerRadius', function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_corner_radius', ['number'], [Number(v)]); }],
    // hover 描边：颜色取该段自己的颜色，圆角沿用 cornerRadius；0 = 关闭
    ['hoverBorderWidth', function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_hover_border_width', ['number'], [Number(v)]); }],
    ['showTags',     function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_show_tags', ['number'], [v ? 1 : 0]); }],
    ['tagFontSize',  function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_tag_font_size', ['number'], [Number(v)]); }],
    ['tagColor',     function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_tag_color', ['number'], [colorValue(v)]); }],
    ['heightRatio',  function (h, v) { withHandle(h, 'kiriyama_sparkbar_set_height_ratio', ['number'], [Number(v)]); }],
    // 气泡与饼图共用同一套（每个实例一份）
    ['tooltipArrowWidth', function (h, v) { withHandle(h, 'kiriyama_tooltip_set_arrow_width', ['number'], [Number(v)]); }],
    ['tooltipFontSize',   function (h, v) { withHandle(h, 'kiriyama_tooltip_set_font_size', ['number'], [Number(v)]); }],
    ['tooltipSide',       function (h, v) { withHandle(h, 'kiriyama_tooltip_set_side', ['number'], [tooltipSideIndex(v)]); }],
    ['tooltipFollowPointer', function (h, v) { withHandle(h, 'kiriyama_tooltip_set_follow_pointer', ['number'], [v ? 1 : 0]); }]
  ];

  /// loadSparkBar(handle, model)
  Module['loadSparkBar'] = function (handle, model) {
    if (typeof model === 'string') {
      model = JSON.parse(model);
    }
    if (!model || !model['series'] || !model['rows']) {
      throw new Error('loadSparkBar: 缺少 model.series / model.rows');
    }

    var h = handle | 0;
    applyOptions(h, model['options'], SPARK_OPTION_SETTERS);

    var series = model['series'];
    var rows = model['rows'];
    withHandle(h, 'kiriyama_sparkbar_begin', [], []);
    for (var s = 0; s < series.length; s++) {
      var item = series[s];
      var name = item['name'] == null ? '' : String(item['name']);
      withHandle(h, 'kiriyama_sparkbar_add_series', ['string', 'number'],
                 [name, colorValue(item['color'])]);
    }
    for (var r = 0; r < rows.length; r++) {
      var row = rows[r];
      withHandle(h, 'kiriyama_sparkbar_begin_row', ['string'],
                 [row['tag'] == null ? '' : String(row['tag'])]);
      var values = row['values'] || [];
      for (var v = 0; v < values.length; v++) {
        var value = Number(values[v]);
        withHandle(h, 'kiriyama_sparkbar_add_value', ['number'], [isNaN(value) ? NaN : value]);
      }
      withHandle(h, 'kiriyama_sparkbar_end_row', [], []);
    }
    withHandle(h, 'kiriyama_sparkbar_commit', [], []);
    return true;
  };

  // ================================================================ LineChart

  function directionFlag(value) {
    if (typeof value === 'number') {
      return value !== 0 ? 1 : 0;
    }
    var text = String(value || '').toLowerCase();
    return (text === 'vertical' || text === 'vert' || text === 'v') ? 1 : 0;
  }

  function curveFlag(value) {
    if (typeof value === 'number') {
      return value !== 0 ? 1 : 0;
    }
    var text = String(value || '').toLowerCase();
    return (text === 'monotone' || text === 'smooth' || text === 'spline') ? 1 : 0;
  }

  var LINE_AXIS_X_SETTERS = [
    ['title',     function (h, v) { withHandle(h, 'kiriyama_line_set_x_title', ['string'], [String(v || '')]); }],
    ['fontSize',  function (h, v) { withHandle(h, 'kiriyama_line_set_x_font_size', ['number'], [Number(v)]); }],
    ['color',     function (h, v) { withHandle(h, 'kiriyama_line_set_x_color', ['number'], [colorValue(v)]); }],
    ['direction', function (h, v) { withHandle(h, 'kiriyama_line_set_x_direction', ['number'], [directionFlag(v)]); }],
    ['angle',     function (h, v) { withHandle(h, 'kiriyama_line_set_x_angle', ['number'], [Number(v)]); }]
  ];

  var LINE_AXIS_Y_SETTERS = [
    ['title',     function (h, v) { withHandle(h, 'kiriyama_line_set_y_title', ['string'], [String(v || '')]); }],
    ['fontSize',  function (h, v) { withHandle(h, 'kiriyama_line_set_y_font_size', ['number'], [Number(v)]); }],
    ['color',     function (h, v) { withHandle(h, 'kiriyama_line_set_y_color', ['number'], [colorValue(v)]); }],
    ['direction', function (h, v) { withHandle(h, 'kiriyama_line_set_y_direction', ['number'], [directionFlag(v)]); }],
    ['angle',     function (h, v) { withHandle(h, 'kiriyama_line_set_y_angle', ['number'], [Number(v)]); }]
  ];

  var LINE_OPTION_SETTERS = [
    ['title',            function (h, v) { withHandle(h, 'kiriyama_line_set_title', ['string'], [String(v || '')]); }],
    ['curve',            function (h, v) { withHandle(h, 'kiriyama_line_set_curve', ['number'], [curveFlag(v)]); }],
    ['lineWidth',        function (h, v) { withHandle(h, 'kiriyama_line_set_line_width', ['number'], [Number(v)]); }],
    ['showArea',         function (h, v) { withHandle(h, 'kiriyama_line_set_show_area', ['number'], [v ? 1 : 0]); }],
    ['areaOpacity',      function (h, v) { withHandle(h, 'kiriyama_line_set_area_opacity', ['number'], [Number(v)]); }],
    ['showGrid',         function (h, v) { withHandle(h, 'kiriyama_line_set_show_grid', ['number'], [v ? 1 : 0]); }],
    ['pointRadius',      function (h, v) { withHandle(h, 'kiriyama_line_set_point_radius', ['number'], [Number(v)]); }],
    ['hoverPointRadius', function (h, v) { withHandle(h, 'kiriyama_line_set_hover_point_radius', ['number'], [Number(v)]); }],
    ['haloRadius',       function (h, v) { withHandle(h, 'kiriyama_line_set_halo_radius', ['number'], [Number(v)]); }],
    ['hitRadius',        function (h, v) { withHandle(h, 'kiriyama_line_set_hit_radius', ['number'], [Number(v)]); }],
    ['xTitle',           function (h, v) { withHandle(h, 'kiriyama_line_set_x_title', ['string'], [String(v || '')]); }],
    ['xFontSize',        function (h, v) { withHandle(h, 'kiriyama_line_set_x_font_size', ['number'], [Number(v)]); }],
    ['xColor',           function (h, v) { withHandle(h, 'kiriyama_line_set_x_color', ['number'], [colorValue(v)]); }],
    ['xDirection',       function (h, v) { withHandle(h, 'kiriyama_line_set_x_direction', ['number'], [directionFlag(v)]); }],
    ['xAngle',           function (h, v) { withHandle(h, 'kiriyama_line_set_x_angle', ['number'], [Number(v)]); }],
    ['yTitle',           function (h, v) { withHandle(h, 'kiriyama_line_set_y_title', ['string'], [String(v || '')]); }],
    ['yFontSize',        function (h, v) { withHandle(h, 'kiriyama_line_set_y_font_size', ['number'], [Number(v)]); }],
    ['yColor',           function (h, v) { withHandle(h, 'kiriyama_line_set_y_color', ['number'], [colorValue(v)]); }],
    ['yDirection',       function (h, v) { withHandle(h, 'kiriyama_line_set_y_direction', ['number'], [directionFlag(v)]); }],
    ['yAngle',           function (h, v) { withHandle(h, 'kiriyama_line_set_y_angle', ['number'], [Number(v)]); }],
    ['xAxis',            function (h, v) { applyOptions(h, v, LINE_AXIS_X_SETTERS); }],
    ['yAxis',            function (h, v) { applyOptions(h, v, LINE_AXIS_Y_SETTERS); }],
    ['tooltipArrowWidth',    function (h, v) { withHandle(h, 'kiriyama_tooltip_set_arrow_width', ['number'], [Number(v)]); }],
    ['tooltipFontSize',      function (h, v) { withHandle(h, 'kiriyama_tooltip_set_font_size', ['number'], [Number(v)]); }],
    ['tooltipSide',          function (h, v) { withHandle(h, 'kiriyama_tooltip_set_side', ['number'], [tooltipSideIndex(v)]); }],
    ['tooltipFollowPointer', function (h, v) { withHandle(h, 'kiriyama_tooltip_set_follow_pointer', ['number'], [v ? 1 : 0]); }]
  ];

  /// loadLine(handle, model)
  Module['loadLine'] = function (handle, model) {
    if (typeof model === 'string') {
      model = JSON.parse(model);
    }
    if (!model || !model['series']) {
      throw new Error('loadLine: 缺少 model.series');
    }

    var h = handle | 0;
    var cfg = model['config'] || model['options'] || {};
    applyOptions(h, cfg, LINE_OPTION_SETTERS);

    var xAxisCfg = cfg['xAxis'] || {};
    var yAxisCfg = cfg['yAxis'] || {};
    var xLabels = model['xLabels'] || model['categories'] || xAxisCfg['labels'] || [];
    var yLabels = model['yLabels'] || yAxisCfg['labels'] || [];
    var series = model['series'] || [];

    withHandle(h, 'kiriyama_line_begin', [], []);
    for (var xi = 0; xi < xLabels.length; xi++) {
      withHandle(h, 'kiriyama_line_add_x_label', ['string'], [String(xLabels[xi])]);
    }
    for (var yi = 0; yi < yLabels.length; yi++) {
      withHandle(h, 'kiriyama_line_add_y_label', ['string'], [String(yLabels[yi])]);
    }
    for (var s = 0; s < series.length; s++) {
      var item = series[s];
      var name = item['name'] == null ? '' : String(item['name']);
      withHandle(h, 'kiriyama_line_begin_series', ['string', 'number'],
                 [name, colorValue(item['color'])]);
      var pts = item['points'] || item['values'] || [];
      for (var p = 0; p < pts.length; p++) {
        var pt = pts[p];
        var px = p;
        var py = NaN;
        if (pt !== null && typeof pt === 'object') {
          px = pt['x'] == null ? p : Number(pt['x']);
          py = pt['y'] == null ? NaN : Number(pt['y']);
        } else if (pt !== null && pt !== undefined) {
          py = Number(pt);
        }
        withHandle(h, 'kiriyama_line_add_point', ['number', 'number'],
                   [isNaN(px) ? p : px, isNaN(py) ? NaN : py]);
      }
      withHandle(h, 'kiriyama_line_end_series', [], []);
    }
    withHandle(h, 'kiriyama_line_commit', [], []);
    return true;
  };

  Module['lineHoveredSeries'] = function (handle) {
    return withHandle(handle | 0, 'kiriyama_line_hovered_series', [], []);
  };

  Module['lineHoveredPoint'] = function (handle) {
    return withHandle(handle | 0, 'kiriyama_line_hovered_point', [], []);
  };

  // ================================================================ 一步挂载
  // mount(type, frame, model) = create + show + load。返回句柄，之后可以
  // showComponent 挪位置、load* 换数据、destroyComponent 摘掉。
  // 同样是 [name, fn] 数组（理由见 PIE_OPTION_SETTERS）：key 来自页面，
  // 对象字面量的键会被 closure 重命名，查不到。
  var LOADERS = [
    ['pie', function (h, model) { Module['loadPie'](h, model); }],
    ['sparkbar', function (h, model) { Module['loadSparkBar'](h, model); }],
    ['table', function (h, model) { Module['loadTable'](h, model); }],
    ['line', function (h, model) { Module['loadLine'](h, model); }]
  ];

  function loadInto(handle, key, model) {
    for (var i = 0; i < LOADERS.length; i++) {
      if (LOADERS[i][0] === key) {
        LOADERS[i][1](handle, model);
        return true;
      }
    }
    return false;
  }

  Module['mount'] = function (type, frame, model) {
    var key = componentKey(type);
    var handle = Module['createComponent'](key);
    if (handle === 0) {
      throw new Error('mount: 创建 "' + key + '" 实例失败');
    }
    if (frame) {
      Module['showComponent'](handle, frame);
    }
    if (model) {
      loadInto(handle, key, model);
    }
    return handle;
  };

  // 允许页面只声明数据：加载完自动送进去（三份各自认领，互不影响）。
  // 包一层而不是覆盖，页面里的 onRuntimeInitialized 还要收 loading 遮罩。
  var shellInit = Module['onRuntimeInitialized'];
  Module['onRuntimeInitialized'] = function () {
    if (shellInit) {
      shellInit.apply(this, arguments);
    }
    // globalThis['x'] 而不是裸变量：closure 会重命名裸标识符。
    if (typeof globalThis['KIRIYAMA_TABLE_MODEL'] !== 'undefined') {
      Module['mount']('table', { x: 0, y: 0, width: 960, height: 540 },
                      globalThis['KIRIYAMA_TABLE_MODEL']);
    }
    if (typeof globalThis['KIRIYAMA_PIE_MODEL'] !== 'undefined') {
      Module['mount']('pie', { x: 0, y: 0, width: 960, height: 540 },
                      globalThis['KIRIYAMA_PIE_MODEL']);
    }
    if (typeof globalThis['KIRIYAMA_SPARKBAR_MODEL'] !== 'undefined') {
      Module['mount']('sparkbar', { x: 0, y: 0, width: 960, height: 540 },
                      globalThis['KIRIYAMA_SPARKBAR_MODEL']);
    }
  };
})();
