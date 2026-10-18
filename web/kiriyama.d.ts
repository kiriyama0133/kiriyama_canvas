// kiriyama/canvas —— TypeScript 声明
//
// 用法（script 标签方式，无需打包）：
//
//   <script src="kiriyama_canvas.js"></script>   // 运行时
//   页面/工程的 tsconfig 里 include 本文件：     // 类型
//     { "include": ["web/kiriyama.d.ts", "web/**/*.ts"] }
//   或者在 .ts 顶部：/// <reference path="./kiriyama.d.ts" />
//
// 文件故意不写 export：浏览器里是全局脚本，所有类型直接挂在全局，
// 页面里写 window.KIRIYAMA_PIE_MODEL 时 IDE 就能给出字段提示。
//
// 分层说明：
//   *Model / *Options  页面侧的数据与样式（对应 entry.js 的 load* 入参）
//   KiriyamaCAPI       wasm 导出的 C 接口（经 Module.ccall 调用；高级用法）
//   Module             运行时胶水层暴露的方法（loadTable / loadPie / …）

declare namespace Kiriyama {
  // ================================================================ 表格

  /** 列类型。排序时按类型比较，不是按字符串。 */
  type CellType = 'text' | 'number' | 'int' | 'integer' | 'bool' | 'date';

  interface TableColumnModel {
    /** 列 id，排序状态按它记。 */
    id: string;
    /** 表头显示名；缺省用 id。 */
    title?: string;
    /** 默认 'text'。 */
    type?: CellType;
    /** 期望列宽（像素），实际会被等比铺满。默认 120。 */
    width?: number;
    /** 默认 true；false 的列不给手型、不可排序。 */
    sortable?: boolean;
    /** 显示格式，传给 snprintf，例如 '%.2f'、'%+.2f%%'。 */
    format?: string;
  }

  type TableCellValue = string | number | boolean | null | undefined;

  interface TableRowModel {
    /** 行身份（排序 / 选中 / 动画都按它）。缺省取第一格。 */
    key?: string;
    cells: TableCellValue[];
  }

  interface TableModel {
    columns: TableColumnModel[];
    rows: TableRowModel[];
  }

  // ================================================================ 饼图

  interface PieSliceModel {
    label: string;
    /** 0 / 负数 / NaN 的片会被跳过（不画、不参与占比）。 */
    value: number;
  }

  interface PieOptions {
    labelFontSize?: number;      // 默认 12.5
    labelColor?: string;         // '#RRGGBB' | 'none'
    showLabels?: boolean;        // 默认 true
    showValues?: boolean;        // 标签里带百分比，默认 true
    gapWidth?: number;           // 扇区缝隙描边，默认 0（关）
    gapColor?: string;           // '#RRGGBB' | 'none'
    explode?: number;            // hover 时外半径放大像素，默认 6
    innerRadius?: number;        // 0..0.95，>0 即甜甜圈
    startAngle?: number;         // 度，12 点方向 = -90
    labelOffset?: number;        // 标签离外圆的距离，默认 10
  }

  interface PieModel {
    slices: PieSliceModel[];
    options?: PieOptions & TooltipOptions;
  }

  // ================================================================ SparkStackBar

  interface SparkBarSeriesModel {
    name: string;
    /** '#RRGGBB' | 'none'；缺省用内置 8 色板。顺序 = 堆叠顺序（自下而上）。 */
    color?: string;
  }

  interface SparkBarRowModel {
    /** 柱下方的标签。 */
    tag: string;
    /** 按 series 顺序，长度可以短于 series（短的部分按缺失）。 */
    values: number[];
  }

  interface SparkBarOptions {
    barWidth?: number;        // 0 = 按可用宽度均分（默认）
    barGap?: number;          // 柱间距，默认 10
    segmentGap?: number;      // 堆叠段缝隙，默认 1.5
    cornerRadius?: number;    // 圆角，默认 3
    hoverBorderWidth?: number; // hover 段的自色描边宽（同一圆角），默认 2；0 = 关闭
    showTags?: boolean;       // 默认 true
    tagFontSize?: number;     // 默认 11
    tagColor?: string;        // '#RRGGBB' | 'none'
    heightRatio?: number;     // 最高柱占可用高度的比例，默认 1
  }

  interface SparkBarModel {
    series: SparkBarSeriesModel[];
    rows: SparkBarRowModel[];
    options?: SparkBarOptions & TooltipOptions;
  }

  // ================================================================ 气泡（共用）

  type TooltipSide = 'above' | 'below' | 'left' | 'right';

  interface TooltipOptions {
    /** 默认 true：气泡像浮层一样跟着光标走；false 则锚在图表上方。 */
    tooltipFollowPointer?: boolean;
    /** 跟随模式下贴哪一侧，默认 'right'。 */
    tooltipSide?: TooltipSide;
    /** 箭头底边宽度，默认 0 = 不画那个倒三角。 */
    tooltipArrowWidth?: number;
    /** 标题字号（正文自动小 1px），默认 13/12。 */
    tooltipFontSize?: number;
  }

  // ================================================================ C 接口
  //
  // 这些是 wasm 的导出函数，正常开发用 Module.load* 就够了；
  // 需要绕过胶水层、自己送数据时才用 ccall：
  //
  //   Module.ccall('kiriyama_pie_set_explode', null, ['number'], [8]);
  //
  // 颜色参数统一是 0xRRGGBB 的数值；负数 = 全透明。

  /** 组件实例句柄：createComponent 返回，>= 1；0 不是有效句柄。 */
  type ComponentHandle = number;

  interface KiriyamaCAPI {
    // ---- 实例生命周期
    kiriyama_create_component(type: string): number;
    kiriyama_destroy_component(handle: ComponentHandle): void;
    kiriyama_clear_components(): void;
    kiriyama_component_count(): number;

    // ---- 挂载（可见性与矩形，逻辑像素）
    kiriyama_show_component(handle: ComponentHandle, x: number, y: number,
                            width: number, height: number): void;
    kiriyama_hide_component(handle: ComponentHandle): void;
    kiriyama_component_visible(handle: ComponentHandle): number;

    // ---- 表格
    kiriyama_table_load(handle: ComponentHandle, text: string): void;

    // ---- 饼图 · 数据
    kiriyama_pie_begin_slices(handle: ComponentHandle): void;
    kiriyama_pie_add_slice(handle: ComponentHandle, label: string, value: number): void;
    kiriyama_pie_commit(handle: ComponentHandle): void;
    /** 行协议：label␟value 每行一片。 */
    kiriyama_pie_load(handle: ComponentHandle, text: string): void;

    // ---- 饼图 · 样式
    kiriyama_pie_set_label_font_size(handle: ComponentHandle, pixels: number): void;
    kiriyama_pie_set_label_color(handle: ComponentHandle, rgb: number): void;
    kiriyama_pie_set_show_labels(handle: ComponentHandle, enabled: number): void;
    kiriyama_pie_set_show_values(handle: ComponentHandle, enabled: number): void;
    kiriyama_pie_set_gap_width(handle: ComponentHandle, pixels: number): void;
    kiriyama_pie_set_gap_color(handle: ComponentHandle, rgb: number): void;
    kiriyama_pie_set_explode(handle: ComponentHandle, pixels: number): void;
    kiriyama_pie_set_inner_radius(handle: ComponentHandle, ratio: number): void;
    kiriyama_pie_set_start_angle(handle: ComponentHandle, degrees: number): void;
    kiriyama_pie_set_label_offset(handle: ComponentHandle, pixels: number): void;

    // ---- 饼图 · 查询
    kiriyama_pie_slice_count(handle: ComponentHandle): number;
    /** 当前 hover 的扇区下标，-1 = 没有。 */
    kiriyama_pie_hovered(handle: ComponentHandle): number;

    // ---- 通用 · 气泡（每个实例一份）
    kiriyama_tooltip_set_arrow_width(handle: ComponentHandle, pixels: number): void;
    kiriyama_tooltip_set_font_size(handle: ComponentHandle, pixels: number): void;
    /** 0 = above / 1 = below / 2 = left / 3 = right。 */
    kiriyama_tooltip_set_side(handle: ComponentHandle, side: number): void;
    kiriyama_tooltip_set_follow_pointer(handle: ComponentHandle, enabled: number): void;

    // ---- SparkStackBar · 数据
    kiriyama_sparkbar_begin(handle: ComponentHandle): void;
    kiriyama_sparkbar_add_series(handle: ComponentHandle, name: string, rgb: number): void;
    kiriyama_sparkbar_begin_row(handle: ComponentHandle, tag: string): void;
    kiriyama_sparkbar_add_value(handle: ComponentHandle, value: number): void;
    kiriyama_sparkbar_end_row(handle: ComponentHandle): void;
    kiriyama_sparkbar_commit(handle: ComponentHandle): void;

    // ---- SparkStackBar · 样式
    kiriyama_sparkbar_set_bar_width(handle: ComponentHandle, pixels: number): void;
    kiriyama_sparkbar_set_bar_gap(handle: ComponentHandle, pixels: number): void;
    kiriyama_sparkbar_set_segment_gap(handle: ComponentHandle, pixels: number): void;
    kiriyama_sparkbar_set_corner_radius(handle: ComponentHandle, pixels: number): void;
    kiriyama_sparkbar_set_hover_border_width(handle: ComponentHandle, pixels: number): void;
    kiriyama_sparkbar_set_tag_font_size(handle: ComponentHandle, pixels: number): void;
    kiriyama_sparkbar_set_tag_color(handle: ComponentHandle, rgb: number): void;
    kiriyama_sparkbar_set_show_tags(handle: ComponentHandle, enabled: number): void;
    kiriyama_sparkbar_set_height_ratio(handle: ComponentHandle, ratio: number): void;
  }

  /** ccall 的签名（emscripten 运行时方法，返回类型字符串见其文档）。 */
  type Ccall = (name: keyof KiriyamaCAPI, returnType: string | null,
                argTypes: string[], args: unknown[]) => unknown;

  // ================================================================ Module

  interface Module {
    canvas?: HTMLCanvasElement;

    onRuntimeInitialized?: () => void;
    onAbort?: (what: string) => void;

    ccall: Ccall;

    /** 创建一个组件实例，返回句柄（>= 1；失败为 0）。 */
    createComponent(type: ComponentType): ComponentHandle;
    /** 销毁实例：节点与它的数据一起释放，句柄之后失效。 */
    destroyComponent(handle: ComponentHandle): void;
    /** 销毁全部实例（画布清空）。 */
    clearComponents(): void;
    componentCount(): number;

    /** 挂到画布上（逻辑像素矩形）。同一句柄再调用 = 改位置与大小。 */
    showComponent(handle: ComponentHandle, frame: FrameRect): void;
    hideComponent(handle: ComponentHandle): void;
    componentVisible(handle: ComponentHandle): boolean;

    /**
     * 页面级主题。描述树只发语义 token，颜色在渲染层按当前主题解析——
     * 切主题不重建任何实例 / 数据，下一帧直接生效。
     */
    setTheme(theme: ThemeName): void;

    /** 一步到位：create + show + load，返回句柄。 */
    mount(type: ComponentType, frame: FrameRect, model: ComponentModel): ComponentHandle;

    /** 载入表格模型。对象或 JSON 文本都行。 */
    loadTable(handle: ComponentHandle, model: TableModel | string): boolean;
    /** 载入饼图模型。 */
    loadPie(handle: ComponentHandle, model: PieModel | string): boolean;
    /** 载入 SparkStackBar 模型。 */
    loadSparkBar(handle: ComponentHandle, model: SparkBarModel | string): boolean;

    /** 当前 hover 的扇区下标（-1 = 没有）。 */
    pieHovered(handle: ComponentHandle): number;
    /** 可用扇区数（0 / 负 / NaN 片不算）。 */
    pieSliceCount(handle: ComponentHandle): number;
  }

  /** 一个 wasm 里装的组件名。 */
  type ComponentType = 'pie' | 'sparkbar' | 'table';

  /** mount() 的第三参数：类型要和第一参数对得上。 */
  type ComponentModel = PieModel | SparkBarModel | TableModel;

  /** 逻辑像素矩形（画布 960×540 的逻辑空间）。 */
  interface FrameRect {
    x: number;
    y: number;
    width: number;
    height: number;
  }

  const ccall: Ccall;
}

/** 运行时胶水对象。页面里通常先 `var Module = { canvas, onRuntimeInitialized }`。 */
declare var Module: Kiriyama.Module | undefined;

interface Window {
  /** 表格页面的模型（entry.js 在运行时初始化后自动送进去）。 */
  KIRIYAMA_TABLE_MODEL?: Kiriyama.TableModel;
  /** 饼图页面的模型。 */
  KIRIYAMA_PIE_MODEL?: Kiriyama.PieModel;
  /** SparkStackBar 页面的模型。 */
  KIRIYAMA_SPARKBAR_MODEL?: Kiriyama.SparkBarModel;
}
