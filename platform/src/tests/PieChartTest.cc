#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "kiriyama/core/Data.h"
#include "kiriyama/core/SceneNode.h"
#include "kiriyama/math/MathUtils.h"
#include "kiriyama/scene/PieChart.h"

namespace {

using kiriyama::core::ChartTheme;
using kiriyama::core::Color;
using kiriyama::core::ColorToken;
using kiriyama::core::kInvalidIndex;
using kiriyama::core::PieChartData;
using kiriyama::core::RectD;
using kiriyama::core::SceneNode;
using kiriyama::core::Vec2d;
using kiriyama::scene::FrameContext;
using kiriyama::scene::PieChart;
using kiriyama::scene::TooltipText;

[[nodiscard]] FrameContext at(double x, double y) {
    FrameContext ctx;
    ctx.time = 0.0;
    ctx.deltaTime = 0.016;
    ctx.pointer.position = Vec2d{x, y};
    ctx.pointer.down = false;
    return ctx;
}

// ---------------------------------------------------------------- 工具

[[nodiscard]] PieChartData fourSlices() {
    PieChartData data;
    data.slices.push_back({"Alpha", 40.0});
    data.slices.push_back({"Beta", 30.0});
    data.slices.push_back({"Gamma", 20.0});
    data.slices.push_back({"Delta", 10.0});
    return data;
}

/// 找到 key = "slice.<index>" 的扇区多边形节点（描述树对账产物）。
/// 树结构：chart → plot（持 frame 平移）→ scene（对账目标）→ slice.<i>。
[[nodiscard]] const SceneNode* findKey(const SceneNode& node, const std::string& key) {
    if (node.key() == key) {
        return &node;
    }
    for (const auto& child : node.children()) {
        if (const SceneNode* hit = findKey(*child, key)) {
            return hit;
        }
    }
    return nullptr;
}

[[nodiscard]] const SceneNode* sliceNode(const PieChart& chart, std::size_t index) {
    return findKey(chart, "slice." + std::to_string(index));
}

[[nodiscard]] std::unique_ptr<PieChart> fourSliceChart() {
    auto chart = std::make_unique<PieChart>("pie");
    chart->setFrame(RectD{0.0, 0.0, 200.0, 200.0});
    kiriyama::core::PieLayoutOptions options;
    options.padding = kiriyama::core::Padding::All(0.0);  // 默认 padding 16，几何断言清零
    chart->setLayoutOptions(options);
    chart->setData(fourSlices());
    chart->tickTree(at(-1.0, -1.0));  // 第一帧：触发重建
    chart->tickTree(at(-1.0, -1.0));
    return chart;
}

// ---------------------------------------------------------------- 布局

TEST(PieChart, BuildsContiguousSectorLayout) {
    const auto chart = fourSliceChart();

    ASSERT_EQ(chart->sliceCount(), 4u);
    const auto& sectors = chart->layout().sectors;
    ASSERT_EQ(sectors.size(), 4u);

    // 角度首尾相接，总覆盖 2π；圆心 = frame 左上角 + 布局圆心。
    EXPECT_DOUBLE_EQ(sectors.back().startAngle + sectors.back().sweepAngle,
                     sectors.front().startAngle + kiriyama::math::kTwoPi);
    EXPECT_DOUBLE_EQ(chart->layout().center.x, 100.0);
    EXPECT_DOUBLE_EQ(chart->layout().center.y, 100.0);
    EXPECT_DOUBLE_EQ(chart->layout().outerRadius, 85.0);  // min(200,200)/2 * 0.85

    // 数据映射：第一片的值 / 占比。
    EXPECT_DOUBLE_EQ(sectors.front().value, 40.0);
    EXPECT_NEAR(sectors.front().fraction, 0.4, 1e-12);
}

TEST(PieChart, ZeroValueSlicesAreSkipped) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    PieChartData data;
    data.slices.push_back({"A", 50.0});
    data.slices.push_back({"B", 0.0});
    data.slices.push_back({"C", 50.0});
    chart.setData(data);
    chart.tickTree(at(-1.0, -1.0));
    chart.tickTree(at(-1.0, -1.0));

    EXPECT_EQ(chart.sliceCount(), 2u);
}

TEST(PieChart, RelayoutsWhenFrameChanges) {
    auto chart = fourSliceChart();
    ASSERT_EQ(chart->sliceCount(), 4u);
    EXPECT_DOUBLE_EQ(chart->layout().center.x, 100.0);
    const std::uint64_t idBefore = sliceNode(*chart, 0)->id();

    chart->setFrame(RectD{100.0, 50.0, 200.0, 200.0});
    chart->tickTree(at(-1.0, -1.0));

    // 布局是组件局部坐标（尺寸不变 → 圆心还是 100,100），
    // 位置变化收敛到组件自身的 transform_ 上，不再需要中间 plot 组，也不触发重建。
    EXPECT_DOUBLE_EQ(chart->layout().center.x, 100.0);
    EXPECT_DOUBLE_EQ(chart->layout().center.y, 100.0);
    const Vec2d moved = chart->transform().apply(Vec2d{0.0, 0.0});
    EXPECT_DOUBLE_EQ(moved.x, 100.0);
    EXPECT_DOUBLE_EQ(moved.y, 50.0);
    EXPECT_EQ(sliceNode(*chart, 0)->id(), idBefore);
}

TEST(PieChart, ClearDataClearsSlices) {
    auto chart = fourSliceChart();
    ASSERT_EQ(chart->sliceCount(), 4u);

    chart->clearData();
    chart->tickTree(at(-1.0, -1.0));
    EXPECT_EQ(chart->sliceCount(), 0u);
    EXPECT_FALSE(chart->valid());
}

TEST(PieChart, DataUpdateKeepsNodeIdentity) {
    // 本测试锁定"描述树 diff"这条更新路径：数据变了，key 相同的扇区节点
    // 必须保住身份（id 不变），而不是整棵拆掉重建。
    auto chart = fourSliceChart();
    ASSERT_NE(sliceNode(*chart, 0), nullptr);
    const std::uint64_t idBefore = sliceNode(*chart, 0)->id();

    PieChartData changed = fourSlices();
    changed.slices[0].value = 10.0;
    chart->setData(changed);
    chart->tickTree(at(-1.0, -1.0));

    const SceneNode* after = sliceNode(*chart, 0);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->id(), idBefore);  // 复用，不是重建

    // 内容跟着数据走：第一片占比 40% → 10/70。
    EXPECT_NEAR(chart->layout().sectors.front().fraction, 10.0 / 70.0, 1e-12);
}

TEST(PieChart, PaletteAppliesOnRebuild) {
    auto chart = fourSliceChart();

    chart->setPalette({Color::Hex(0xFF0000)});
    chart->tickTree(at(-1.0, -1.0));

    // 扇区颜色是语义 token（Token::Series + seriesIndex）——主题在渲染层解析。
    // 实例色板挂在对账根组上：沿树继承、指针稳定（palette 没变就复用）。
    const SceneNode* slice = sliceNode(*chart, 0);
    ASSERT_NE(slice, nullptr);
    EXPECT_EQ(slice->style().fill.token, ColorToken::Series);
    EXPECT_EQ(slice->style().fill.seriesIndex, 0u);

    const SceneNode* inner = chart->children().front().get();
    ASSERT_NE(inner, nullptr);
    ASSERT_TRUE(inner->style().seriesPalette);
    ASSERT_FALSE(inner->style().seriesPalette->empty());
    // 实例色板解析出组件给的颜色；换主题不影响（实例色板优先于全局）。
    EXPECT_EQ(slice->style().fill.resolve(ChartTheme::Dark(),
                                          inner->style().seriesPalette.get()),
              Color::Hex(0xFF0000));
}

// ---------------------------------------------------------------- 命中 / hover

TEST(PieChart, HoverFollowsPointerAcrossSectors) {
    auto chart = std::make_unique<PieChart>("pie");
    chart->setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    // 等值四片：每个象限正好一片（-90°→0° / 0°→90° / 90°→180° / 180°→270°）。
    PieChartData data;
    data.slices.push_back({"Alpha", 25.0});
    data.slices.push_back({"Beta", 25.0});
    data.slices.push_back({"Gamma", 25.0});
    data.slices.push_back({"Delta", 25.0});
    chart->setData(data);
    chart->tickTree(at(-1.0, -1.0));  // 重建

    // 片 0 是右上象限（-90°→0°），圆心 (100,100)，取角平分线上的点。
    const Vec2d alpha{100.0 + 40.0 / std::sqrt(2.0), 100.0 - 40.0 / std::sqrt(2.0)};
    chart->tickTree(at(alpha.x, alpha.y));
    ASSERT_NE(chart->hoveredIndex(), kInvalidIndex);
    EXPECT_EQ(chart->hoveredIndex(), 0u);

    // 移到片 2（左下象限 90°→180°）。
    const Vec2d gamma{100.0 - 40.0 / std::sqrt(2.0), 100.0 + 40.0 / std::sqrt(2.0)};
    chart->tickTree(at(gamma.x, gamma.y));
    EXPECT_EQ(chart->hoveredIndex(), 2u);

    chart->tickTree(at(-300.0, -300.0));
    EXPECT_EQ(chart->hoveredIndex(), kInvalidIndex);
}

TEST(PieChart, DonutHoleIsNotHit) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    kiriyama::core::PieLayoutOptions options;
    options.padding = kiriyama::core::Padding::All(0.0);
    options.innerRadiusRatio = 0.5;
    chart.setLayoutOptions(options);
    chart.setData(fourSlices());
    chart.tickTree(at(-1.0, -1.0));

    // 圆心在内孔里：不命中（组件 hitShape 与 hoveredIndex 都不算）。
    EXPECT_FALSE(chart.hitTest(Vec2d{100.0, 100.0}));
    chart.tickTree(at(100.0, 100.0));
    EXPECT_EQ(chart.hoveredIndex(), kInvalidIndex);

    // 环带里：命中片 0（右上象限）。
    const Vec2d alpha{100.0 + 60.0 / std::sqrt(2.0), 100.0 - 60.0 / std::sqrt(2.0)};
    chart.tickTree(at(alpha.x, alpha.y));
    EXPECT_EQ(chart.hoveredIndex(), 0u);
}

TEST(PieChart, ExplodeGrowsRadiusAndHitArea) {
    auto chart = fourSliceChart();
    chart->setSliceDefaults(6.0);

    const Vec2d alpha{100.0 + 40.0 / std::sqrt(2.0), 100.0 - 40.0 / std::sqrt(2.0)};
    chart->tickTree(at(alpha.x, alpha.y));

    // 一步喂足 0.12s 的动画时长，外扩走满。
    FrameContext dwell = at(alpha.x, alpha.y);
    dwell.deltaTime = 0.5;
    chart->tickTree(dwell);
    EXPECT_DOUBLE_EQ(chart->explodeRadius(0), 6.0);
    EXPECT_DOUBLE_EQ(chart->explodeRadius(1), 0.0);

    // 外扩后命中区跟着变大：半径 88（> 85 基准外半径）能点中片 0。
    const Vec2d beyond{100.0 + 88.0 / std::sqrt(2.0), 100.0 - 88.0 / std::sqrt(2.0)};
    chart->tickTree(at(beyond.x, beyond.y));
    EXPECT_EQ(chart->hoveredIndex(), 0u);
}

// ---------------------------------------------------------------- 气泡

TEST(PieChart, DefaultTooltipIsLabelValuePercent) {
    auto chart = fourSliceChart();

    const Vec2d alpha{100.0 + 40.0 / std::sqrt(2.0), 100.0 - 40.0 / std::sqrt(2.0)};
    chart->tickTree(at(alpha.x, alpha.y));

    const TooltipText text = chart->tooltipContent();
    EXPECT_EQ(text.title, "Alpha");
    EXPECT_EQ(text.body, "40 · 40.0%");
}

TEST(PieChart, StaticContentOverridesDefault) {
    auto chart = fourSliceChart();
    chart->setTooltipContent("分类", "自定义内容");

    const TooltipText text = chart->tooltipContent();
    EXPECT_EQ(text.title, "分类");
    EXPECT_EQ(text.body, "自定义内容");
}

TEST(PieChart, AnchorIsChartFrame) {
    // 气泡锚在整张图表的 frame 上（hover 哪片都浮在图上方，不压扇区）。
    auto chart = fourSliceChart();
    EXPECT_EQ(chart->tooltipAnchor(), chart->frame());
}

}  // namespace
