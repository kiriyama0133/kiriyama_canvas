// ---------------------------------------------------------------- entry.js
//
// 页面与 wasm 之间的唯一胶水层，table / pie 两个 demo 共用。
// 构建时用 --post-js 拼进 table.js / pie.js，运行在 Module 的作用域里，
// 因此这里能直接用 ccall 这类运行时方法，页面则完全不需要知道
// 编码格式、字符串指针、导出符号名。
//
// 页面最省事的写法（连一行 JS 都不用写）：
//
//     <script>
//       window.KIRIYAMA_TABLE_MODEL = { columns: [...], rows: [...] };
//       // 或
//       window.KIRIYAMA_PIE_MODEL = { slices: [{ label, value }, ...] };
//     </script>
//     <script src="table.js"></script>   // 或 pie.js
//
// 运行时换数据 / 多份数据：
//
//     Module.loadTable(model);          // 普通对象
//     Module.loadTable(jsonText);       // JSON 字符串（fetch 回来不用 parse）
//     Module.loadPie(model);
//
// 两个 loader 都注册在每个产物里，互不干扰：各自只认自己的 MODEL 全局，
// 没挂就什么都不做。属性一律用 Module['x'] 访问：--closure=1 会重命名点号属性。
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

  // 行协议，见 src/app/TableDemo.cc 的 parseModel：
  //   Cidtitletypewidthsortableformat
  //   Rkeycell…
  // U+001F 在日常文本里不会出现，所以不需要转义。
  var UNIT = '\u001f';

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

  /// 载入一份表格模型。model 可以是对象，也可以是 JSON 文本。
  /// 只是登记，下一帧绘制时才替换场景树（不在 JS 调用栈上动 C++ 对象）。
  Module['loadTable'] = function (model) {
    if (typeof model === 'string') {
      model = JSON.parse(model);
    }
    if (!model || !model['columns']) {
      throw new Error('loadTable: 缺少 model.columns');
    }
    Module['ccall']('kiriyama_table_load', null, ['string'], [encodeTable(model)]);
    return true;
  };

  // ---------------------------------------------------------------- 饼图
  // 数据：label␟value 每行一片（src/app/PieDemo.cc 的 parseModel）。
  // 样式：options 对象在这里逐项翻译成 kiriyama_pie_set_* 导出函数——
  // 前端交付的是 JS，样式映射不塞进 C++。颜色 '#RRGGBB' 解析成 0xRRGGBB，
  // 'none' 传 -1（C++ 侧负值 = 全透明）。
  function encodePie(model) {
    var slices = model['slices'] || [];
    var lines = [];
    for (var i = 0; i < slices.length; i++) {
      var slice = slices[i];
      var label = slice['label'] == null ? '' : String(slice['label']);
      var value = slice['value'] == null ? '' : String(slice['value']);
      lines.push(label + UNIT + value);
    }
    return lines.join('\n');
  }

  function pieColor(value) {
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

  var PIE_OPTION_SETTERS = {
    labelFontSize: function (v) { Module['ccall']('kiriyama_pie_set_label_font_size', null, ['number'], [Number(v)]); },
    labelColor:    function (v) { Module['ccall']('kiriyama_pie_set_label_color', null, ['number'], [pieColor(v)]); },
    showLabels:    function (v) { Module['ccall']('kiriyama_pie_set_show_labels', null, ['number'], [v ? 1 : 0]); },
    showValues:    function (v) { Module['ccall']('kiriyama_pie_set_show_values', null, ['number'], [v ? 1 : 0]); },
    gapWidth:      function (v) { Module['ccall']('kiriyama_pie_set_gap_width', null, ['number'], [Number(v)]); },
    gapColor:      function (v) { Module['ccall']('kiriyama_pie_set_gap_color', null, ['number'], [pieColor(v)]); },
    explode:       function (v) { Module['ccall']('kiriyama_pie_set_explode', null, ['number'], [Number(v)]); },
    innerRadius:   function (v) { Module['ccall']('kiriyama_pie_set_inner_radius', null, ['number'], [Number(v)]); },
    startAngle:    function (v) { Module['ccall']('kiriyama_pie_set_start_angle', null, ['number'], [Number(v)]); },
    labelOffset:   function (v) { Module['ccall']('kiriyama_pie_set_label_offset', null, ['number'], [Number(v)]); }
  };

  function applyPieOptions(options) {
    if (!options || typeof options !== 'object') {
      return;
    }
    for (var key in options) {
      if (Object.prototype.hasOwnProperty.call(options, key) && PIE_OPTION_SETTERS[key]) {
        PIE_OPTION_SETTERS[key](options[key]);
      }
    }
  }

  Module['loadPie'] = function (model) {
    if (typeof model === 'string') {
      model = JSON.parse(model);
    }
    if (!model || !model['slices']) {
      throw new Error('loadPie: 缺少 model.slices');
    }
    applyPieOptions(model['options']);
    Module['ccall']('kiriyama_pie_load', null, ['string'], [encodePie(model)]);
    return true;
  };

  // 允许页面只声明数据：加载完自动送进去（table / pie 各自认领，互不影响）。
  // 包一层而不是覆盖，shell 里的 onRuntimeInitialized 还要收 loading 遮罩。
  var shellInit = Module['onRuntimeInitialized'];
  Module['onRuntimeInitialized'] = function () {
    if (shellInit) {
      shellInit.apply(this, arguments);
    }
    // globalThis['x'] 而不是裸变量：closure 会重命名裸标识符。
    if (typeof globalThis['KIRIYAMA_TABLE_MODEL'] !== 'undefined') {
      Module['loadTable'](globalThis['KIRIYAMA_TABLE_MODEL']);
    }
    if (typeof globalThis['KIRIYAMA_PIE_MODEL'] !== 'undefined') {
      Module['loadPie'](globalThis['KIRIYAMA_PIE_MODEL']);
    }
  };
})();
