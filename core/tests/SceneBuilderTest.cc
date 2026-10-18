#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "kiriyama/core/SceneBuilder.h"
#include "kiriyama/core/SceneQuery.h"

namespace {

using namespace kiriyama::core;

Dataset makeLineDataset() {
    Dataset dataset;
    dataset.categories = {"Mon", "Tue", "Wed", "Thu", "Fri"};

    Series revenue;
    revenue.name = "revenue";
    for (int i = 0; i < 5; ++i) {
        revenue.addPoint(static_cast<double>(i), 10.0 + static_cast<double>(i) * 5.0);
    }
    Series cost;
    cost.name = "cost";
    for (int i = 0; i < 5; ++i) {
        cost.addPoint(static_cast<double>(i), 8.0 + static_cast<double>(i) * 2.0);
    }
    dataset.series = {revenue, cost};
    return dataset;
}

Dataset makeBarDataset() {
    Dataset dataset;
    dataset.categories = {"A", "B", "C"};
    Series sales;
    sales.name = "sales";
    sales.addPoint(0.0, 4.0);
    sales.addPoint(1.0, 7.0);
    sales.addPoint(2.0, 2.0);
    dataset.series = {sales};
    return dataset;
}

std::vector<const SceneNode*> nodesOfType(const SceneNode& root, NodeType type) {
    std::vector<const SceneNode*> found;
    forEachDepthFirst(root, [&](const SceneNode& node) {
        if (node.type() == type) {
            found.push_back(&node);
        }
    });
    return found;
}

}  // namespace

// ---------------------------------------------------------------- 折线

TEST(SceneBuilder, LineChartBuildsStableTree) {
    const Dataset dataset = makeLineDataset();
    const auto result = buildLineChart(dataset, {800.0, 450.0});

    ASSERT_TRUE(result.root);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.root->key(), "chart");

    const SceneNode* background = findByKey(*result.root, "background");
    ASSERT_NE(background, nullptr);
    EXPECT_EQ(background->frame(), (RectD{0.0, 0.0, 800.0, 450.0}));
    EXPECT_EQ(background->type(), NodeType::Rect);

    const SceneNode* plot = findByKey(*result.root, "plot.bg");
    ASSERT_NE(plot, nullptr);
    EXPECT_EQ(plot->frame(), result.layout.plot);

    // 两个系列各一条折线，点数和数据一致
    const SceneNode* line = findByKey(*result.root, "series:revenue.seg.0.line");
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->type(), NodeType::Polyline);
    EXPECT_EQ(line->points().size(), 5u);
    EXPECT_TRUE(line->style().hasStroke());
    EXPECT_GT(line->style().strokeWidth, 0.0);

    // 首尾点必须落在绘图区内
    const auto& points = line->points();
    EXPECT_NEAR(points.front().x, result.layout.plot.left(), 1e-6);
    EXPECT_NEAR(points.back().x, result.layout.plot.right(), 1e-6);
    for (const Vec2d& point : points) {
        EXPECT_GE(point.y, result.layout.plot.top() - 1e-6);
        EXPECT_LE(point.y, result.layout.plot.bottom() + 1e-6);
    }

    // 网格线 + 轴标签都在
    EXPECT_NE(findByKey(*result.root, "grid"), nullptr);
    EXPECT_NE(findByKey(*result.root, "axis.x.line"), nullptr);
    EXPECT_NE(findByKey(*result.root, "axis.y.line"), nullptr);
    EXPECT_NE(findByKey(*result.root, "axis.x.tick.0"), nullptr);
    EXPECT_NE(findByKey(*result.root, "axis.y.tick.0"), nullptr);
}

TEST(SceneBuilder, LineChartBreaksAtNullValues) {
    Dataset dataset;
    Series gappy;
    gappy.name = "gappy";
    gappy.addPoint(0.0, 1.0);
    gappy.addNull(1.0);
    gappy.addNull(2.0);
    gappy.addPoint(3.0, 4.0);
    gappy.addPoint(4.0, 6.0);
    dataset.series = {gappy};

    const auto result = buildLineChart(dataset, {600.0, 400.0});
    ASSERT_TRUE(result.valid);

    // 第 0 段只有一个点：折线画不出来，退化成一个点
    const SceneNode* dot = findByKey(*result.root, "series:gappy.seg.0.dot");
    ASSERT_NE(dot, nullptr);
    EXPECT_EQ(dot->type(), NodeType::Ellipse);

    // 第 1 段是 3.0 / 4.0 两个点
    const SceneNode* line = findByKey(*result.root, "series:gappy.seg.1.line");
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->points().size(), 2u);
    EXPECT_NEAR(result.layout.fromPixelX(line->points().front().x), 3.0, 1e-6);

    // 空值被记进 issues
    EXPECT_EQ(result.issues.nullValues, 2u);
}

TEST(SceneBuilder, MonotoneCurveStaysInsideDataRange) {
    Dataset dataset;
    Series zigzag;
    zigzag.name = "zig";
    zigzag.addPoint(0.0, 0.0);
    zigzag.addPoint(1.0, 10.0);
    zigzag.addPoint(2.0, 0.0);
    zigzag.addPoint(3.0, 10.0);
    dataset.series = {zigzag};

    SceneBuildOptions options;
    options.curve = LineCurve::Monotone;
    options.curveSegments = 8;

    const auto result = buildLineChart(dataset, {600.0, 400.0}, {}, options);
    ASSERT_TRUE(result.valid);

    const SceneNode* line = findByKey(*result.root, "series:zig.seg.0.line");
    ASSERT_NE(line, nullptr);
    ASSERT_GT(line->points().size(), 4u);

    // 像素区间：不过冲意味着采样点不会跑出数据的 y 范围
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (const DataPoint& point : dataset.series.front().points) {
        const double y = result.layout.toPixelY(point.y);
        low = std::min(low, y);
        high = std::max(high, y);
    }
    for (const Vec2d& sample : line->points()) {
        EXPECT_GE(sample.y, low - 1e-6);
        EXPECT_LE(sample.y, high + 1e-6);
    }

    // 曲线必须穿过原始数据点（第 1 个数据点位于第 curveSegments 个采样点）
    const auto& points = line->points();
    EXPECT_NEAR(points.front().y, result.layout.toPixelY(0.0), 1e-6);
    const std::size_t second = static_cast<std::size_t>(options.curveSegments);
    ASSERT_LT(second, points.size());
    EXPECT_NEAR(points[second].y, result.layout.toPixelY(10.0), 1e-6);
}

TEST(SceneBuilder, LogAxisProducesNoNaNAndMonotonePixels) {
    Dataset dataset;
    Series growth;
    growth.name = "growth";
    const double values[] = {1.0, 10.0, 1000.0, 1.0e6};
    for (int i = 0; i < 4; ++i) {
        growth.addPoint(static_cast<double>(i), values[i]);
    }
    dataset.series = {growth};

    LayoutOptions layoutOptions;
    layoutOptions.y.scale = ScaleType::Log;

    const auto result = buildLineChart(dataset, {700.0, 400.0}, layoutOptions);
    ASSERT_TRUE(result.valid);

    const SceneNode* line = findByKey(*result.root, "series:growth.seg.0.line");
    ASSERT_NE(line, nullptr);
    for (const Vec2d& point : line->points()) {
        EXPECT_TRUE(std::isfinite(point.x));
        EXPECT_TRUE(std::isfinite(point.y));
    }
    // 采样点必须和坐标轴映射完全一致（对数轴上尤其容易算错）
    const auto& points = line->points();
    EXPECT_NEAR(points.front().y, result.layout.toPixelY(1.0), 1e-6);
    EXPECT_NEAR(points.back().y, result.layout.toPixelY(1.0e6), 1e-6);
    EXPECT_NE(result.layout.toPixelY(1.0), result.layout.toPixelY(1.0e6));
}

// ---------------------------------------------------------------- 柱状

TEST(SceneBuilder, BarRectsHitTestToTheirNodes) {
    const Dataset dataset = makeBarDataset();
    const auto result = buildBarChart(dataset, {640.0, 400.0});

    ASSERT_TRUE(result.valid);
    ASSERT_EQ(result.bars.size(), 3u);

    for (const BarRect& bar : result.bars) {
        const auto hit = hitTestScene(*result.root, bar.rect.center());
        ASSERT_TRUE(hit) << "bar " << bar.categoryIndex << " 没命中";
        EXPECT_EQ(hit.node->key(), "bar." + std::to_string(bar.seriesIndex) + "." +
                                       std::to_string(bar.categoryIndex));
        EXPECT_EQ(hit.node->type(), NodeType::Rect);
    }

    // 类目轴标签直接用类目名
    const SceneNode* tick = findByKey(*result.root, "axis.x.tick.0");
    ASSERT_NE(tick, nullptr);
    EXPECT_EQ(tick->text(), "A");

    // 类目轴默认不画竖向网格
    EXPECT_TRUE(nodesOfType(*result.root, NodeType::Line).size() > 0u);
    EXPECT_EQ(findByKey(*result.root, "grid.x.0"), nullptr);
}

TEST(SceneBuilder, BarValueLabelsSkipShortBars) {
    Dataset dataset = makeBarDataset();
    dataset.series.front().points[2].y = 0.02;  // 几乎为零，留不出标签高度

    SceneBuildOptions options;
    options.showValueLabels = true;

    const auto result = buildBarChart(dataset, {640.0, 400.0}, {}, options);
    ASSERT_TRUE(result.valid);

    EXPECT_NE(findByKey(*result.root, "bar.value.0.1"), nullptr);
    EXPECT_EQ(findByKey(*result.root, "bar.value.0.2"), nullptr);
}

TEST(SceneBuilder, StackedBarsShareCategoryBand) {
    Dataset dataset = makeBarDataset();
    Series other;
    other.name = "other";
    other.addPoint(0.0, 3.0);
    other.addPoint(1.0, 1.0);
    other.addPoint(2.0, 5.0);
    dataset.series.push_back(other);

    SceneBuildOptions options;
    options.barMode = BarMode::Stacked;

    const auto result = buildBarChart(dataset, {640.0, 400.0}, {}, options);
    ASSERT_TRUE(result.valid);
    ASSERT_EQ(result.bars.size(), 6u);

    for (std::size_t i = 0; i < result.bars.size(); ++i) {
        for (std::size_t j = i + 1; j < result.bars.size(); ++j) {
            if (result.bars[i].categoryIndex == result.bars[j].categoryIndex) {
                // 同类别堆叠：宽度一致、上下相接、不重叠
                EXPECT_NEAR(result.bars[i].rect.x, result.bars[j].rect.x, 1e-6);
                EXPECT_NEAR(result.bars[i].rect.width, result.bars[j].rect.width, 1e-6);
                const double gap = std::abs(result.bars[i].rect.bottom() -
                                            result.bars[j].rect.top());
                const double gap2 = std::abs(result.bars[j].rect.bottom() -
                                             result.bars[i].rect.top());
                EXPECT_TRUE(gap < 1e-6 || gap2 < 1e-6);
            }
        }
    }
}

// ---------------------------------------------------------------- 饼图

TEST(SceneBuilder, PieSectorsAreHittablePolygons) {
    PieChartData data;
    data.slices = {{"A", 1.0}, {"B", 1.0}, {"C", 2.0}};

    const auto result = buildPieChart(data, {400.0, 400.0});
    ASSERT_TRUE(result.valid);
    ASSERT_EQ(result.pie.sectors.size(), 3u);

    EXPECT_EQ(nodesOfType(*result.root, NodeType::Polygon).size(), 3u);

    // 第一片：12 点方向起、顺时针 90°，中角 -45°
    const PieSector& first = result.pie.sectors.front();
    const double radius = (result.pie.innerRadius + result.pie.outerRadius) * 0.5;
    const Vec2d probe{result.pie.center.x + std::cos(first.midAngle) * radius,
                      result.pie.center.y + std::sin(first.midAngle) * radius};

    const auto hit = hitTestScene(*result.root, probe);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit.node->key(), "slice.0");

    // 圆心在扇区之外（pie 的圆心是多个扇区的公共点，命中最上层即可）
    EXPECT_NE(findByKey(*result.root, "slice.label.0"), nullptr);
}

TEST(SceneBuilder, DonutCentreIsNotHittable) {
    PieChartData data;
    data.slices = {{"A", 1.0}, {"B", 1.0}};

    SceneBuildOptions options;
    options.pie.innerRadiusRatio = 0.6;

    const auto result = buildPieChart(data, {400.0, 400.0}, options);
    ASSERT_TRUE(result.valid);
    EXPECT_GT(result.pie.innerRadius, 0.0);

    // 甜甜圈中心：没有任何扇区覆盖（命中的只能是背景）
    const auto hit = hitTestScene(*result.root, result.pie.center);
    if (hit) {
        EXPECT_EQ(hit.node->key(), "background");
    }
}

// ---------------------------------------------------------------- 标题 / 空数据

TEST(SceneBuilder, TitleReservesTopSpace) {
    const Dataset dataset = makeLineDataset();

    const auto plain = buildLineChart(dataset, {800.0, 450.0});

    SceneBuildOptions options;
    options.title = "Revenue by day";
    const auto titled = buildLineChart(dataset, {800.0, 450.0}, {}, options);

    ASSERT_TRUE(titled.valid);
    EXPECT_GT(titled.layout.plot.top(), plain.layout.plot.top());

    const SceneNode* title = findByKey(*titled.root, "title");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), "Revenue by day");
    EXPECT_LE(title->frame().bottom(), titled.layout.plot.top());
}

TEST(SceneBuilder, EmptyDatasetRendersEmptyState) {
    const Dataset empty;
    const auto result = buildLineChart(empty, {800.0, 450.0});

    ASSERT_TRUE(result.root);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(findByKey(*result.root, "background"), nullptr);
    EXPECT_NE(findByKey(*result.root, "empty.text"), nullptr);
    EXPECT_EQ(findByKey(*result.root, "series"), nullptr);
}

// ---------------------------------------------------------------- 主题 token

TEST(SceneBuilder, SameTreeResolvesDifferentColorsPerTheme) {
    const Dataset dataset = makeBarDataset();
    const auto result = buildBarChart(dataset, {640.0, 400.0});
    ASSERT_TRUE(result.valid);

    // 同一棵树：背景 / 网格 / 系列色都是 token，换主题 = 换解析结果。
    const SceneNode* background = findByKey(*result.root, "background");
    const SceneNode* grid = findByKey(*result.root, "grid.y.0");
    const SceneNode* bar = findByKey(*result.root, "bar.0.0");
    ASSERT_NE(background, nullptr);
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(bar, nullptr);

    const ChartTheme light = ChartTheme::Light();
    const ChartTheme dark = ChartTheme::Dark();

    EXPECT_EQ(background->style().fill.token, ColorToken::Background);
    EXPECT_EQ(background->style().fill.resolve(light), light.background);
    EXPECT_EQ(background->style().fill.resolve(dark), dark.background);
    EXPECT_NE(background->style().fill.resolve(light).a == 0,
              background->style().fill.resolve(dark).a == 0);

    EXPECT_EQ(grid->style().stroke.token, ColorToken::Grid);
    EXPECT_EQ(grid->style().stroke.resolve(light), light.grid);
    EXPECT_EQ(grid->style().stroke.resolve(dark), dark.grid);

    EXPECT_EQ(bar->style().fill.token, ColorToken::Series);
    EXPECT_EQ(bar->style().fill.resolve(light), light.seriesColor(0));
    EXPECT_EQ(bar->style().fill.resolve(dark), dark.seriesColor(0));
}

TEST(SceneBuilder, ThemeSwitchRequiresNoReconciliation) {
    // 树不依赖主题：同一数据 build 两次，对账零写入——切主题根本不用走 diff。
    const Dataset dataset = makeLineDataset();

    SceneBuildOptions options;
    options.labelFontSize = 13.0;  // 度量是布局参数，与主题无关
    const auto first = buildLineChart(dataset, {800.0, 450.0}, {}, options);
    const auto second = buildLineChart(dataset, {800.0, 450.0}, {}, options);
    ASSERT_TRUE(first.valid);
    ASSERT_TRUE(second.valid);

    SceneNode renderRoot(NodeType::Group, "chart");
    (void)patchScene(renderRoot, first);
    const std::size_t nodeCount = countNodes(renderRoot);

    const auto diff = patchScene(renderRoot, second);
    EXPECT_EQ(diff.stats.inserted, 0u);
    EXPECT_EQ(diff.stats.removed, 0u);
    EXPECT_EQ(diff.stats.moved, 0u);
    EXPECT_EQ(diff.stats.updated, 0u);
    EXPECT_TRUE(diff.changed.empty());
    EXPECT_EQ(countNodes(renderRoot), nodeCount);
}

// ---------------------------------------------------------------- 双轴

TEST(SceneBuilder, SecondarySeriesUsesRightAxis) {
    const Dataset dataset = makeLineDataset();

    SceneBuildOptions options;
    options.secondarySeriesCount = 1;  // cost 走右轴

    const auto result = buildLineChart(dataset, {800.0, 450.0}, {}, options);
    ASSERT_TRUE(result.valid);
    ASSERT_TRUE(result.layout.hasSecondaryY);

    EXPECT_NE(findByKey(*result.root, "axis.y2.line"), nullptr);
    EXPECT_NE(findByKey(*result.root, "axis.y2.tick.0"), nullptr);

    // 两条线的 y 像素不能完全一致：各自量程不同
    const SceneNode* primary = findByKey(*result.root, "series:revenue.seg.0.line");
    const SceneNode* secondary = findByKey(*result.root, "series:cost.seg.0.line");
    ASSERT_NE(primary, nullptr);
    ASSERT_NE(secondary, nullptr);
    EXPECT_GT(std::abs(primary->points().front().y - secondary->points().front().y), 1.0);
}

// ---------------------------------------------------------------- 增量

TEST(SceneBuilder, PatchReusesNodesWhenOnlyValuesChange) {
    Dataset dataset = makeLineDataset();
    const Vec2d size{800.0, 450.0};

    SceneNode renderRoot(NodeType::Group, "chart");
    const auto first = buildLineChart(dataset, size);
    const auto firstDiff = patchScene(renderRoot, first);
    EXPECT_GT(firstDiff.stats.inserted, 0u);

    const SceneNode* line = findByKey(renderRoot, "series:revenue.seg.0.line");
    ASSERT_NE(line, nullptr);
    const std::uint64_t lineId = line->id();
    const std::size_t nodeCount = countNodes(renderRoot);

    // 只改一个数据点
    dataset.series[0].points[2].y = 99.0;
    const auto second = buildLineChart(dataset, size);
    const auto diff = patchScene(renderRoot, second);

    EXPECT_EQ(diff.stats.inserted, 0u);
    EXPECT_EQ(diff.stats.removed, 0u);
    EXPECT_EQ(diff.stats.moved, 0u);
    EXPECT_GT(diff.stats.updated, 0u);
    EXPECT_EQ(countNodes(renderRoot), nodeCount);

    // 节点身份没变，只有属性被改写
    const SceneNode* sameLine = findByKey(renderRoot, "series:revenue.seg.0.line");
    ASSERT_NE(sameLine, nullptr);
    EXPECT_EQ(sameLine->id(), lineId);
    EXPECT_NE(std::find(diff.changed.begin(), diff.changed.end(), sameLine), diff.changed.end());
}

TEST(SceneBuilder, PatchKeepsSeriesIdentityAcrossReorder) {
    Dataset dataset = makeLineDataset();
    const Vec2d size{800.0, 450.0};

    SceneNode renderRoot(NodeType::Group, "chart");
    (void)patchScene(renderRoot, buildLineChart(dataset, size));

    const SceneNode* revenue = findByKey(renderRoot, "series:revenue.seg.0.line");
    ASSERT_NE(revenue, nullptr);
    const std::uint64_t revenueId = revenue->id();

    // 交换两个系列的顺序：key 是名字，节点应该被复用而不是重建
    std::swap(dataset.series[0], dataset.series[1]);
    const auto diff = patchScene(renderRoot, buildLineChart(dataset, size));

    EXPECT_EQ(diff.stats.inserted, 0u);
    EXPECT_EQ(diff.stats.removed, 0u);
    EXPECT_GT(diff.stats.moved, 0u);

    const SceneNode* stillRevenue = findByKey(renderRoot, "series:revenue.seg.0.line");
    ASSERT_NE(stillRevenue, nullptr);
    EXPECT_EQ(stillRevenue->id(), revenueId);
}
