#pragma once

// 描述树构建：Dataset / PieChartData + ChartLayout -> SceneNode 树。
//
// 这是"数据 -> 图形描述"的唯一出口，配合 SceneDiff 组成完整增量链路：
//   1. 数据每次变化都 buildScene() 重新生成一棵描述树（只描述，不持有渲染资源）；
//   2. reconcileScene(渲染树, *result.root) 就地更新，只有变化的节点会被标脏；
//   3. 渲染层遍历渲染树，跳过 hasDirtyInSubtree() == false 的整棵分支。
//
// key 约定（diff 复用的基础，同一父节点内唯一）：
//   background / plot.bg / empty
//   grid.x.<i> / grid.y.<i> / axis.x.line / axis.y.line
//   axis.x.tick.<i> / axis.y.tick.<i> / axis.x.title / axis.y.title
//   series:<名称> 或 series#<下标>        每个系列一个 Group
//   seg.<k> / area.<k> / p.<j>            折线段 / 面积 / 顶点
//   bar:<系列>.<类目>                      柱形
//   slice.<i>                             饼图扇区
//
// 系列 key 优先用系列名（名称在整个数据集里唯一时），否则退化成下标：
// "改值 / 改顺序"能保住节点身份，"改名"会被当成换了一个系列。
//
// 折线的断点：连续的有效点组成一条 Polyline，遇到空值即断开；
// 孤立点（前后都是空值）单独输出一个 Ellipse，否则它什么都画不出来。
//
// 饼图扇区用 Polygon 逼近圆弧（SceneNode 没有 Arc 图元）。细分数量按弧长自适应，
// 默认整圆 96 段——半径 200px 时弦高误差不到 0.1px，肉眼无感，且能被 polygonContains 直接命中。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Data.h"
#include "Layout.h"
#include "Scene.h"
#include "SceneDiff.h"
#include "ScrollLayout.h"
#include "SparkBar.h"
#include "Table.h"
#include "TableLayout.h"
#include "Theme.h"

namespace kiriyama::core {

// ---------------------------------------------------------------- 主题
//
// ChartTheme（纯颜色调色板）与 ColorToken / Paint 见 Theme.h。
// 描述树只发 token，渲染层用当前主题解析——切深浅色不需要 rebuild。
// 字号 / 线宽等度量是布局参数，在 SceneBuildOptions 里，不在 theme 里。

// ---------------------------------------------------------------- 选项

enum class ChartKind : std::uint8_t {
    Line,
    Bar,
    Pie,
};

enum class LineCurve : std::uint8_t {
    Linear,    ///< 折线
    Monotone,  ///< 单调三次插值：不过冲，不会在陡增段鼓出假峰值
};

struct SceneBuildOptions {
    /// 描述树的颜色一律语义 token（Theme.h），这里不再有 theme 颜色字段；
    /// 主题在渲染层（当前是页面级状态）。

    // ---- 文本 / 线宽度量（布局参数：影响文字排版盒与描边几何，所以在这里）
    double labelFontSize{12.0};
    double titleFontSize{15.0};
    double gridWidth{1.0};
    double axisWidth{1.0};

    /// 图表标题，空则不占位（会额外撑开顶部 padding）。
    std::string title{};
    /// 最后 N 个系列改用右轴；0 表示单轴。右轴的刻度与标题取自 LayoutOptions::secondaryY。
    std::size_t secondarySeriesCount{0};

    // ---- 折线
    LineCurve curve{LineCurve::Linear};
    double lineWidth{2.0};
    /// Monotone 下每段曲线的细分数（1 即退化成折线）。
    int curveSegments{16};
    bool showPoints{false};
    double pointRadius{3.0};
    bool showArea{false};
    double areaOpacity{0.16};

    // ---- 柱状
    BarMode barMode{BarMode::Grouped};
    BarOptions barOptions{};
    double barCornerRadius{0.0};
    double barOpacity{1.0};
    /// 柱顶数值标签（柱太窄或太矮时自动省略）。
    bool showValueLabels{false};

    // ---- 网格 / 轴 / 标题
    bool showGrid{true};
    /// 类目轴默认不画竖向网格线——每条都压在一组柱子上，只会显乱。
    bool showCategoryGrid{false};
    bool showPlotBackground{true};
    bool showAxisLines{true};
    bool showAxisLabels{true};
    bool showAxisTitles{true};
    bool showChartTitle{true};
    double titleGap{10.0};

    // ---- 饼图
    PieLayoutOptions pie{};
    bool showPieLabels{true};
    bool showPieValues{true};
    /// 整圆细分数；扇区按弧长比例取用。
    int pieSegments{96};
    /// 扇区之间的描边宽度（形成分隔缝）。
    double pieSliceGap{1.0};
    /// 缝隙描边色；alpha == 0 时发 Token::Background（用背景色"擦"出缝，跟主题走）。
    Color pieGapColor{};
    /// 扇区标签文字色覆盖；alpha == 0 时发 Token::Label（跟随主题）。
    /// 组件级自定义标签色（如 PieChart::setLabelStyle）从这里进。
    Color pieLabelColorOverride{};

    // ---- 空状态
    bool showEmptyText{true};
    std::string emptyText{"No data"};

    /// 描述树里文本定位（标签 / 刻度 / 标题）用的测量回调。
    /// 为空时用 defaultTextMeasure 估算；宿主接真实字体度量可消除估宽误差。
    TextMeasureFn textMeasure{};
};

// ---------------------------------------------------------------- 入口

struct ChartSpec {
    ChartKind kind{ChartKind::Line};
    Dataset dataset{};
    PieChartData pie{};
    Vec2d canvasSize{800.0, 450.0};
    LayoutOptions layout{};
    SceneBuildOptions options{};
};

struct SceneBuildResult {
    /// 描述树根节点。交给 reconcileScene 后即可丢弃。
    std::unique_ptr<SceneNode> root{};
    ChartLayout layout{};
    PieLayout pie{};
    /// 柱形几何，与 BarMode 对应，方便 tooltip 反查数值。
    std::vector<BarRect> bars{};
    DataIssues issues{};
    /// 有可渲染内容时为 true；空数据 / 画布过小为 false。
    bool valid{false};

    [[nodiscard]] bool empty() const noexcept { return root == nullptr; }
};

[[nodiscard]] SceneBuildResult buildScene(const ChartSpec& spec);

[[nodiscard]] SceneBuildResult buildLineChart(const Dataset& dataset, Vec2d canvasSize,
                                             const LayoutOptions& layoutOptions = {},
                                             const SceneBuildOptions& options = {});

[[nodiscard]] SceneBuildResult buildBarChart(const Dataset& dataset, Vec2d canvasSize,
                                            const LayoutOptions& layoutOptions = {},
                                            const SceneBuildOptions& options = {});

[[nodiscard]] SceneBuildResult buildPieChart(const PieChartData& data, Vec2d canvasSize,
                                            const SceneBuildOptions& options = {});

// ---------------------------------------------------------------- PieChart 扇区描述树与快路径

struct PieStyle {
    PieLayoutOptions layout{};
    bool showLabels{true};
    bool showValues{true};
    double labelFontSize{12.5};
    Color labelColor{Color::Hex(0x5A5A54)};
    double gapWidth{0.0};
    Color gapColor{Color::Hex(0xFFFFFF)};
    int segments{96};
    double explodeDistance{6.0};
    TextMeasureFn textMeasure{};
};

struct PieSlicesBuildResult {
    std::unique_ptr<SceneNode> root{};
    PieLayout layout{};
    bool valid{false};
};

/// 直接构建饼图扇区与外侧标签描述树（根节点 key = "slices"），供 PieChart 组件直接对账，
/// 避免先构建外层卡片再从中拆解子树。
[[nodiscard]] PieSlicesBuildResult buildPieSlices(
    const PieChartData& data, Vec2d canvasSize, const PieStyle& style = {},
    std::shared_ptr<const std::vector<Color>> seriesPalette = nullptr);

/// 就地 O(1) 更新已对账饼图树中指定扇区（slice.<sectorIndex>）的外扩多边形几何，
/// 使交互层无需缓存扇区节点裸指针或耦合内部 key 命名。
void updatePieSliceExplode(SceneNode& slicesRoot, const PieLayout& layout,
                           std::size_t sectorIndex, double radiusDelta, int segments = 96);

// ---------------------------------------------------------------- SparkBar 描述树

struct SparkBarBuildResult {
    std::unique_ptr<SceneNode> root{};
    SparkBarLayout layout{};
    bool valid{false};
};

/// 一步完成 SparkBar 布局计算 + 描述树构建（横向百分比堆叠条 / 行标题 / hover 外描边 / 底部图例）。
[[nodiscard]] SparkBarBuildResult buildSparkBar(
    const SparkBarData& data, Vec2d canvasSize, const SparkBarOptions& options = {},
    std::span<const Color> palette = {},
    std::size_t hoveredItem = SparkBarPick::kNone,
    std::size_t hoveredSegment = SparkBarPick::kNone);

/// 就地 O(1) 更新已对账 SparkBar 树里的 hover 外描边（hover.outline），避免指针移动时重建整棵树。
void updateSparkBarHover(SceneNode& sparkRoot, const SparkBarLayout& layout,
                         const SparkBarOptions& options, std::span<const Color> palette,
                         std::size_t hoveredItem, std::size_t hoveredSegment);

// ---------------------------------------------------------------- 表格描述树

struct TableStyle {
    Paint background{ColorToken::Background};
    Paint headerBackground{ColorToken::PlotBackground};
    Paint headerText{ColorToken::Label};
    Paint headerActiveText{ColorToken::Title};
    Paint rowText{ColorToken::Title};
    Paint mutedText{ColorToken::EmptyText};
    Paint grid{ColorToken::Grid};
    Paint border{ColorToken::Grid};
    Paint hoverRow{ColorToken::Grid, 0.45f};
    Paint selectedRow{Paint::series(0, 0.14f)};
    Paint accent{Paint::series(0)};
    /// 涨=红 / 跌=绿（国内行情惯例）。只作用于"格式化串里带 +"的列。
    Paint positive{Paint::series(4)};
    Paint negative{Paint::series(5)};

    double radius{10.0};
    double headerFontSize{11.0};
    double cellFontSize{12.5};
    TableMetrics metrics{};
};

struct TableBuildResult {
    std::unique_ptr<SceneNode> root{};
    TableLayout layout{};
    std::vector<double> columnWidths{};
};

/// 构建表格背景、固定表头（含跟随横向滚动的列标题与排序箭头）及空数据提示。
/// 返回根节点 key = "table.chrome" 的 Group。
[[nodiscard]] std::unique_ptr<SceneNode> buildTableChrome(
    const TableData& data, const TableState& state, const TableLayout& layout,
    Vec2d canvasSize, const TableStyle& style = {}, double scrollX = 0.0);

/// 构建表格正文可见行与高亮节点（处于未扣滚动的逻辑内容空间 y = position * rowHeight，
/// 可直接交给 ScrollWidget::content 对账复用，实现纯平移滚动与局部增量行切片）。
[[nodiscard]] std::unique_ptr<SceneNode> buildTableBody(
    const TableData& data, const TableState& state, const TableLayout& layout,
    Vec2d canvasSize, const TableStyle& style = {}, int hoveredPosition = kNoRow);

/// 就地 O(1) 同步表头列组的横向滚动平移（-scrollX），与 ScrollWidget 正文横向滚动保持锁定。
void updateTableHeaderScroll(SceneNode& chromeRoot, double scrollX) noexcept;

/// 一步完成表格列宽测量（或复用 cachedColumnWidths）+ 视口布局计算 + 描述树构建。
[[nodiscard]] TableBuildResult buildTable(
    const TableData& data, const TableState& state, Vec2d canvasSize,
    double scrollOffset = 0.0, const TableStyle& style = {},
    int hoveredPosition = kNoRow, std::span<const double> cachedColumnWidths = {},
    const TextMeasureFn& textMeasure = {});

/// 构建完整表格描述树（卡片底 / 表头 / 排序三角 / 带裁剪的可见行与单元格 / 外框）。
/// 返回根节点 key = "table" 的 Group，可直接交给 reconcileScene。
[[nodiscard]] std::unique_ptr<SceneNode> buildTable(
    const TableData& data, const TableState& state, const TableLayout& layout,
    Vec2d canvasSize, const TableStyle& style = {}, int hoveredPosition = kNoRow);

/// 就地更新已对账表格正文树（或完整表格树）里的 hover / 选中高亮节点
/// （row.hover.bg / row.selected.bg / row.selected.accent），
/// 供交互层在指针移动或行点击选中时 O(1) 更新，避免重跑整棵 buildTableBody + reconcileScene。
void updateTableHighlights(SceneNode& bodyOrTableRoot, const TableLayout& layout, Vec2d canvasSize,
                           const TableStyle& style, int hoveredPosition, int selectedPosition);

// ---------------------------------------------------------------- 滚动条描述树（ScrollWidget / 通用滚动容器）

struct ScrollBarStyle {
    ScrollBarMetrics metrics{};

    /// 容器底色与外框（默认透明无框，可按需开启卡片化背景与边框）
    Paint background{ColorToken::None};
    Paint border{ColorToken::None};
    double borderWidth{0.0};
    double containerRadius{0.0};

    /// 滚动槽（Track）、右下交汇角（Corner）与滑块（Thumb）的三态语义色
    Paint trackFill{ColorToken::Grid, 0.42f};
    Paint cornerFill{ColorToken::None};
    Paint thumbFill{ColorToken::AxisLine, 0.65f};
    Paint thumbHoverFill{ColorToken::Label, 0.85f};
    Paint thumbActiveFill{Paint::series(0, 0.95f)};
};

struct ScrollBarVisualState {
    bool verticalHovered{false};
    bool verticalActive{false};
    bool horizontalHovered{false};
    bool horizontalActive{false};
};

/// 构建滚动容器的装饰与双轴滚动条描述树（返回根节点 key = "scroll.chrome"）：
///   · "scroll.v.track" / "scroll.v.thumb"（右侧纵向滚动槽与滑块）
///   · "scroll.h.track" / "scroll.h.thumb"（底部横向滚动槽与滑块）
///   · "scroll.corner"（横纵滚动条同时可见时的右下角交汇块）
///   · "scroll.border"（可选容器外框）
[[nodiscard]] std::unique_ptr<SceneNode> buildScrollBars(
    const ScrollLayout& layout, Vec2d containerSize,
    const ScrollBarStyle& style = {},
    const ScrollBarVisualState& state = {});

/// 就地 O(1) 更新已对账 scroll.chrome 树里的轨道/滑块位置、可见性及三态颜色（常态 / 悬停 / 按压拖拽），
/// 供高频滚动或滑块拖拽期间直接调用，避免每帧重跑 buildScrollBars + reconcileScene。
void updateScrollBars(SceneNode& chromeRoot, const ScrollLayout& layout, Vec2d containerSize,
                      const ScrollBarStyle& style = {},
                      const ScrollBarVisualState& state = {});

/// 把描述树对账进被维护的渲染树。等价于 reconcileScene(target, *description.root)。
[[nodiscard]] SceneDiffResult patchScene(SceneNode& target, const SceneBuildResult& description);

}  // namespace kiriyama::core
