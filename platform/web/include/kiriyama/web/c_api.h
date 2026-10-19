#pragma once

// kiriyama/canvas 的统一 C API（web 平台）。
//
// 命名约定：kiriyama_<组件>_<动作>。以后 line / bar / table 的接口都加在这个
// 头文件里，实现都在同目录的 c_api.cc——调用方只认一个入口文件。
//
// ---------------------------------------------------------------- 实例模型
// 一个 wasm 里装着一套组件类型；页面想画几个就创建几个**实例**，每个实例
// 自带一份数据、一份样式、一块矩形。两个饼图互不影响，靠的是句柄而不是全局量：
//
//   int a = kiriyama_create_component("pie");
//   int b = kiriyama_create_component("pie");
//   kiriyama_pie_load(a, "Technology␟38.6\nConsumer␟21.4");
//   kiriyama_pie_load(b, "Bonds␟40\nCash␟60");
//   kiriyama_show_component(a,   0, 0, 470, 540);
//   kiriyama_show_component(b, 490, 0, 470, 540);
//
// 句柄是 int：0 不是有效句柄（创建失败也返回 0），销毁后失效。
// 所有 kiriyama_<组件>_* 的第一个参数都是句柄。
//
// ⚠️ 以前数据放在文件级单例（gData / gSparkData / gTableData），"一个进程一份"，
// 第二个图表必然把第一个的数据顶掉——那是 bug，不是设计。现在状态跟着实例走。
//
// 三层接口（都带句柄）：
//
//   1. 数据（批量提交，一次重建）
//        kiriyama_pie_begin_slices(a);
//        kiriyama_pie_add_slice(a, "Technology", 38.6);
//        kiriyama_pie_commit(a);              // 整批换数据；下一帧才重建场景节点
//
//      便捷入口 kiriyama_pie_load(a, "label␟value\n…")。
//
//   2. 样式（逐项设置，只标脏，下一帧统一应用）
//        kiriyama_pie_set_label_font_size(a, 14);
//        kiriyama_pie_set_label_color(a, 0x3A3A38);   // 0xRRGGBB；负值 = 透明
//
//   3. 查询
//        kiriyama_pie_slice_count(a)   可用扇区数（0/负/NaN 片不算）
//        kiriyama_pie_hovered(a)       当前 hover 的扇区下标，-1 = 没有
//
// 线程约定：全部只能在宿主线程（渲染线程）调用，和 SDL 一致。
// 所有 kiriyama_* 函数都不在调用栈上动场景树——只登记，由宿主每帧
// pump*() 统一消费，避免 JS 回调里重入渲染。
//
// ⚠️ 实现约定（踩过坑）：kiriyama_* 的**定义**必须在全局作用域、带
// EMSCRIPTEN_KEEPALIVE。放进 namespace 会和这里的 extern "C" 声明成为两个实体，
// 符号被 C++ 修饰，wasm 导出表里查不到，JS 侧 ccall 报 "a is not a function"。

#include <vector>

#include "kiriyama/core/Data.h"
#include "kiriyama/core/Layout.h"
#include "kiriyama/core/SparkBar.h"
#include "kiriyama/render/TooltipHost.h"
#include "kiriyama/scene/PieChart.h"
#include "kiriyama/scene/SparkBar.h"
#include "kiriyama/scene/TableWidget.h"

extern "C" {

// ================================================================ 实例生命周期

/// 创建一个组件实例。type: "pie" / "sparkbar" / "table"。
/// 返回句柄（>= 1），类型不认识返回 0。
int kiriyama_create_component(const char* type);

/// 销毁实例：节点与它的数据一起释放。句柄之后失效（再传进来会被忽略）。
void kiriyama_destroy_component(int handle);

/// 销毁全部实例（画布清空）。
void kiriyama_clear_components(void);

/// 当前存活的实例数。
int kiriyama_component_count(void);

// ================================================================ 挂载
// 实例创建出来是"未挂载"的：show 一次才上画布。不可见的实例不 tick、不绘制、
// 不弹气泡，但数据与样式照旧可以改（改完再 show 就是新的）。

/// x/y/width/height 是逻辑像素坐标；width 或 height <= 0 时忽略。
void kiriyama_show_component(int handle, double x, double y, double width, double height);

void kiriyama_hide_component(int handle);

/// 1 = 已挂载，0 = 未挂载或句柄无效。
int kiriyama_component_visible(int handle);

// ================================================================ 饼图 · 数据

/// 开始一批扇区数据。会清空上一个未提交的缓冲。
void kiriyama_pie_begin_slices(int handle);

/// 追加一片。label 为空或 value 非有限值的片会被跳过（口径和数据清洗一致）。
void kiriyama_pie_add_slice(int handle, const char* label, double value);

/// 提交这一批：整批替换数据，version +1，下一帧重建扇区节点。
void kiriyama_pie_commit(int handle);

/// 一次性入口，行协议：`label␟value` 每行一片（U+001F 分隔）。
void kiriyama_pie_load(int handle, const char* text);

// ================================================================ 饼图 · 样式
// 颜色约定：0..0xFFFFFF = 0xRRGGBB；负值 = 全透明。

void kiriyama_pie_set_label_font_size(int handle, double pixels);
void kiriyama_pie_set_label_color(int handle, double rgb);
void kiriyama_pie_set_show_labels(int handle, double enabled);
void kiriyama_pie_set_show_values(int handle, double enabled);
void kiriyama_pie_set_gap_width(int handle, double pixels);
void kiriyama_pie_set_gap_color(int handle, double rgb);
/// hover 时扇区外半径放大的像素数（默认 6，0 = 关）。
void kiriyama_pie_set_explode(int handle, double pixels);
/// 0..0.95，> 0 即甜甜圈。
void kiriyama_pie_set_inner_radius(int handle, double ratio);
/// 起始角，单位度，12 点方向 = -90。
void kiriyama_pie_set_start_angle(int handle, double degrees);
/// 标签锚点离外圆的距离。
void kiriyama_pie_set_label_offset(int handle, double pixels);

// ================================================================ 饼图 · 查询

int kiriyama_pie_slice_count(int handle);
int kiriyama_pie_hovered(int handle);

// ================================================================ 通用 · 气泡
// 每个实例一份气泡配置（两个饼图可以一个贴上一个贴右）。宿主在绘制前把
// 当前指针所在实例的配置喂给唯一的 TooltipHost。

/// 箭头底边宽度，0 = 不画（默认）。给 8~12 才画那个倒三角。
void kiriyama_tooltip_set_arrow_width(int handle, double pixels);
/// 标题字号；正文自动比它小 1px。<= 0 表示用组件默认。
void kiriyama_tooltip_set_font_size(int handle, double pixels);
/// 贴哪一侧：0 = above（默认，浮在图上方）、1 = below、2 = left、3 = right。
/// 放不下时自动翻面；组件若显式设了 side 则优先于这里。
void kiriyama_tooltip_set_side(int handle, double side);
/// 气泡跟随鼠标（默认开）：气泡像浮层一样跟着光标走，不固定在图表某一处。
/// 关掉则锚在组件自己的锚点上（图表组件锚在整块图表上）。
void kiriyama_tooltip_set_follow_pointer(int handle, double enabled);

// ================================================================ SparkStackBar · 数据
// 组装顺序（a 是句柄）：
//   kiriyama_sparkbar_begin(a)
//   kiriyama_sparkbar_add_series(a, "Revenue", 0x3E6DB5)   // 决定堆叠顺序与颜色
//   kiriyama_sparkbar_begin_row(a, "2022")
//   kiriyama_sparkbar_add_value(a, 10.0)                    // 按 series 顺序
//   kiriyama_sparkbar_commit(a)

void kiriyama_sparkbar_begin(int handle);
/// 追加一个堆叠层。颜色 0xRRGGBB；负值 = 透明（渲染时退回默认色板）。
void kiriyama_sparkbar_add_series(int handle, const char* name, double rgb);
void kiriyama_sparkbar_begin_row(int handle, const char* tag);
void kiriyama_sparkbar_add_value(int handle, double value);
void kiriyama_sparkbar_end_row(int handle);
void kiriyama_sparkbar_commit(int handle);

// ================================================================ SparkStackBar · 样式

/// 柱宽；0 = 按可用宽度均分（默认）。
void kiriyama_sparkbar_set_bar_width(int handle, double pixels);
void kiriyama_sparkbar_set_bar_gap(int handle, double pixels);
/// 堆叠段之间的缝隙。
void kiriyama_sparkbar_set_segment_gap(int handle, double pixels);
void kiriyama_sparkbar_set_corner_radius(int handle, double pixels);
/// hover 时给命中的那一段描一圈它自己的颜色（同一圆角）。0 = 关闭，默认 2。
void kiriyama_sparkbar_set_hover_border_width(int handle, double pixels);
void kiriyama_sparkbar_set_tag_font_size(int handle, double pixels);
void kiriyama_sparkbar_set_tag_color(int handle, double rgb);
void kiriyama_sparkbar_set_show_tags(int handle, double enabled);
/// 最高柱占可用高度的比例（0..1）。
void kiriyama_sparkbar_set_height_ratio(int handle, double ratio);

// ================================================================ Table · 数据
// 行协议（entry.js 的 encodeTable 生成、这里 parse）：
//   C␟id␟title␟type␟width␟sortable␟format     列定义（type: text/number/int/bool/date）
//   R␟key␟cell1␟cell2␟...                     一行；空串 = 缺失（不是空字符串）

void kiriyama_table_load(int handle, const char* text);

}  // extern "C"

namespace kiriyama::web {

/// 组件类型。
enum class Component : int {
    Pie = 0,
    SparkBar = 1,
    Table = 2,
};

/// 宿主用来同步实例集合：一个句柄 = 一个实例。
struct InstanceInfo {
    int handle{0};
    Component type{Component::Pie};
    bool visible{false};
    core::RectD frame{};
};

/// 当前存活的实例（顺序稳定：按句柄升序）。宿主每帧diff 一次，增删节点。
[[nodiscard]] std::vector<InstanceInfo> instances();

/// 当前页面主题（token 解析来源）。kiriyama_set_theme 改这里。
[[nodiscard]] const core::ChartTheme& currentTheme();

/// 宿主每帧调用：把这个实例 pending 的数据 / 样式落到组件上。
/// 放在 tickTree 之前（数据 / 样式变化会触发组件内部重建）。
/// 组件持有 model（setData 拷贝），槽里的数据只是 pending 缓冲。
void pumpPie(int handle, scene::PieChart& chart);

/// SparkStackBar 的宿主侧消费：pending 数据 / 样式 → 组件。
void pumpSparkBar(int handle, scene::SparkBar& bar);

/// Table 的宿主侧消费：pending 数据 → 组件。
void pumpTable(int handle, scene::TableWidget& table);

/// 把一个实例的气泡配置喂给宿主唯一的 TooltipHost。
/// 指针落在哪个实例上就喂哪个（气泡语义互斥，一页只有一个）。
void applyTooltip(int handle, render::TooltipHost& tooltip);

/// 宿主上报某实例本帧 hover 的扇区下标（-1 = 没有），供 kiriyama_pie_hovered() 查询。
void reportHoveredSlice(int handle, int index);

}  // namespace kiriyama::web
