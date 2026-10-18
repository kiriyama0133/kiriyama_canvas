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

/// 把描述树对账进被维护的渲染树。等价于 reconcileScene(target, *description.root)。
[[nodiscard]] SceneDiffResult patchScene(SceneNode& target, const SceneBuildResult& description);

}  // namespace kiriyama::core
