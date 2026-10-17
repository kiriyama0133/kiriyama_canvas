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

namespace kiriyama::core {

// ---------------------------------------------------------------- 主题

/// 配色与字号。默认浅色，深色主题换一套色板即可，不改任何几何。
struct ChartTheme {
    Color background{Color::FromBytes(0xFF, 0xFF, 0xFF)};
    Color plotBackground{Color::FromBytes(0xFA, 0xFA, 0xF8)};
    Color grid{Color::FromBytes(0xE9, 0xE7, 0xE1)};
    Color axisLine{Color::FromBytes(0xB4, 0xB2, 0xAA)};
    Color label{Color::FromBytes(0x6E, 0x6E, 0x68)};
    Color title{Color::FromBytes(0x2C, 0x2C, 0x2A)};
    Color emptyText{Color::FromBytes(0xA8, 0xA6, 0x9E)};

    double gridWidth{1.0};
    double axisWidth{1.0};
    double labelFontSize{12.0};
    double titleFontSize{15.0};

    /// 为空时用 defaultSeriesPalette()。
    std::vector<Color> series{};

    [[nodiscard]] static ChartTheme Light();
    [[nodiscard]] static ChartTheme Dark();

    /// 超过色板长度自动循环。
    [[nodiscard]] Color seriesColor(std::size_t index) const;
};

/// 8 色分类色板（蓝 / 青 / 橙 / 紫 / 红 / 绿 / 灰蓝 / 玫红）。
[[nodiscard]] const std::vector<Color>& defaultSeriesPalette();

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
    ChartTheme theme{ChartTheme::Light()};

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
    /// 扇区之间的描边宽度（用背景色描边，形成分隔缝）。
    double pieSliceGap{1.0};

    // ---- 空状态
    bool showEmptyText{true};
    std::string emptyText{"No data"};
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
