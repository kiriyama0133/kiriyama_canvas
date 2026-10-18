#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "kiriyama/core/Data.h"
#include "kiriyama/math/MathUtils.h"
#include "kiriyama/scene/PieChart.h"
#include "kiriyama/scene/PieSlice.h"

namespace {

using kiriyama::core::ArcD;
using kiriyama::core::Color;
using kiriyama::core::PieChartData;
using kiriyama::core::RectD;
using kiriyama::core::Vec2d;
using kiriyama::scene::FrameContext;
using kiriyama::scene::PieChart;
// core::PieSlice（数据片）和 scene::PieSlice（扇形组件）同名，测试里用别名区分。
using SceneSlice = kiriyama::scene::PieSlice;
using kiriyama::scene::TooltipText;

[[nodiscard]] FrameContext at(double x, double y) {
    FrameContext ctx;
    ctx.time = 0.0;
    ctx.deltaTime = 0.016;
    ctx.pointer.position = Vec2d{x, y};
    ctx.pointer.down = false;
    return ctx;
}

/// 右上象限扇形：start = -90°，sweep = 90°，圆心在原点。
[[nodiscard]] ArcD quarterArc(double innerRadius = 0.0, double outerRadius = 100.0) {
    ArcD arc;
    arc.center = Vec2d{0.0, 0.0};
    arc.innerRadius = innerRadius;
    arc.outerRadius = outerRadius;
    arc.startAngle = -kiriyama::math::kHalfPi;
    arc.sweepAngle = kiriyama::math::kHalfPi;
    return arc;
}

[[nodiscard]] std::unique_ptr<SceneSlice> quarterSlice(double innerRadius = 0.0) {
    auto slice = std::make_unique<SceneSlice>("s0");
    slice->setArc(quarterArc(innerRadius));
    slice->setData("A", 25.0, 0.25);
    return slice;
}

// ---------------------------------------------------------------- PieSlice 命中

TEST(PieSlice, PolarHitTestMatchesAngleAndRadius) {
    const auto slice = quarterSlice();

    // 角平分线（-45°）上的点命中。
    EXPECT_TRUE(slice->hitTest(Vec2d{50.0, -50.0}));
    // 圆心：实心饼算命中。
    EXPECT_TRUE(slice->hitTest(Vec2d{0.0, 0.0}));
    // 第二象限：角度不在扇区内。
    EXPECT_FALSE(slice->hitTest(Vec2d{-50.0, -50.0}));
    // 第四象限：同样不在。
    EXPECT_FALSE(slice->hitTest(Vec2d{50.0, 50.0}));
    // 外半径之外。
    EXPECT_FALSE(slice->hitTest(Vec2d{120.0, -120.0}));
}

TEST(PieSlice, DonutHoleIsNotHit) {
    const auto slice = quarterSlice(50.0);

    EXPECT_TRUE(slice->hitTest(Vec2d{50.0, -50.0}));    // 半径 70.7，落在环带里
    EXPECT_FALSE(slice->hitTest(Vec2d{10.0, -10.0}));   // 半径 14.1，内孔
    EXPECT_FALSE(slice->hitTest(Vec2d{0.0, 0.0}));      // 圆心
}

TEST(PieSlice, HitTestFollowsExplode) {
    const auto slice = quarterSlice();
    slice->setExplodeDistance(6.0);

    // 半径 103（-45° 方向）：原扇区外，外半径放大后进入。
    const Vec2d beyond{103.0 / std::sqrt(2.0), -103.0 / std::sqrt(2.0)};
    EXPECT_FALSE(slice->hitTest(beyond));

    slice->tickTree(at(50.0, -50.0));  // 悬停角平分线上的点
    // 外扩走满（一步喂够 0.12s 的时长），否则偏移量太小，103 的点还进不来。
    FrameContext dwell = at(50.0, -50.0);
    dwell.deltaTime = 0.5;
    slice->tickTree(dwell);
    EXPECT_DOUBLE_EQ(slice->explodeProgress(), 1.0);
    EXPECT_TRUE(slice->hitTest(beyond));
}

TEST(PieSlice, ExplodeGrowsRadiusNotOffset) {
    const auto slice = quarterSlice();
    slice->setExplodeDistance(6.0);

    FrameContext inside = at(50.0, -50.0);
    inside.deltaTime = 0.5;  // 一步走满 0.12s 的时长
    slice->tickTree(inside);
    EXPECT_DOUBLE_EQ(slice->explodeProgress(), 1.0);
    EXPECT_DOUBLE_EQ(slice->explodeRadius(), 6.0);

    FrameContext outside = at(-300.0, -300.0);
    outside.deltaTime = 0.5;
    slice->tickTree(outside);
    EXPECT_DOUBLE_EQ(slice->explodeProgress(), 0.0);
    EXPECT_DOUBLE_EQ(slice->explodeRadius(), 0.0);
}

// ---------------------------------------------------------------- PieSlice 气泡

TEST(PieSlice, DefaultTooltipIsLabelValuePercent) {
    const auto slice = quarterSlice();
    const TooltipText text = slice->tooltipContent();
    EXPECT_EQ(text.title, "A");
    EXPECT_EQ(text.body, "25 · 25.0%");
}

TEST(PieSlice, StaticContentOverridesDefault) {
    const auto slice = quarterSlice();
    slice->setTooltipContent("分类", "自定义内容");

    const TooltipText text = slice->tooltipContent();
    EXPECT_EQ(text.title, "分类");
    EXPECT_EQ(text.body, "自定义内容");
}

TEST(PieSlice, AnchorSitsAtMidRadius) {
    const auto slice = quarterSlice();
    slice->setTooltipContent("t", "b");

    const RectD anchor = slice->tooltipAnchor();
    const double midRadius = 50.0;                       // (0 + 100) / 2
    const double expected = midRadius / std::sqrt(2.0);  // -45° 方向
    EXPECT_NEAR(anchor.x + anchor.width * 0.5, expected, 1e-9);
    EXPECT_NEAR(anchor.y + anchor.height * 0.5, -expected, 1e-9);
}

// ---------------------------------------------------------------- PieChart

[[nodiscard]] PieChartData fourSlices() {
    PieChartData data;
    data.slices.push_back({"Alpha", 40.0});
    data.slices.push_back({"Beta", 30.0});
    data.slices.push_back({"Gamma", 20.0});
    data.slices.push_back({"Delta", 10.0});
    return data;
}

TEST(PieChart, BuildsSlicesWithContiguousAngles) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    kiriyama::core::PieLayoutOptions options;
    options.padding = kiriyama::core::Padding::All(0.0);  // 默认 padding 16，这里为了几何断言清零
    chart.setLayoutOptions(options);

    PieChartData data = fourSlices();
    chart.setData(&data);
    chart.tickTree(at(-1.0, -1.0));  // 第一帧：触发重建
    chart.tickTree(at(-1.0, -1.0));

    ASSERT_EQ(chart.sliceCount(), 4u);
    ASSERT_NE(chart.sliceAt(0), nullptr);
    EXPECT_EQ(chart.sliceAt(0)->label(), "Alpha");
    EXPECT_DOUBLE_EQ(chart.sliceAt(0)->value(), 40.0);
    EXPECT_NEAR(chart.sliceAt(0)->fraction(), 0.4, 1e-12);

    // 角度首尾相接，总覆盖 2π；圆心 = frame 左上角 + 布局圆心。
    const ArcD& first = chart.sliceAt(0)->arc();
    const ArcD& last = chart.sliceAt(3)->arc();
    EXPECT_DOUBLE_EQ(last.startAngle + last.sweepAngle, first.startAngle + kiriyama::math::kTwoPi);
    EXPECT_DOUBLE_EQ(first.center.x, 100.0);
    EXPECT_DOUBLE_EQ(first.center.y, 100.0);
    EXPECT_DOUBLE_EQ(first.outerRadius, 85.0);  // min(200,200)/2 * 0.85
}

TEST(PieChart, ZeroValueSlicesAreSkipped) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    PieChartData data;
    data.slices.push_back({"A", 50.0});
    data.slices.push_back({"B", 0.0});
    data.slices.push_back({"C", 50.0});
    chart.setData(&data);
    chart.tickTree(at(-1.0, -1.0));
    chart.tickTree(at(-1.0, -1.0));

    EXPECT_EQ(chart.sliceCount(), 2u);
    EXPECT_EQ(chart.sliceAt(0)->label(), "A");
    EXPECT_EQ(chart.sliceAt(1)->label(), "C");
}

TEST(PieChart, RelayoutsWhenFrameChanges) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    PieChartData data = fourSlices();
    chart.setData(&data);
    chart.tickTree(at(-1.0, -1.0));
    chart.tickTree(at(-1.0, -1.0));
    ASSERT_EQ(chart.sliceCount(), 4u);
    EXPECT_DOUBLE_EQ(chart.sliceAt(0)->arc().center.x, 100.0);

    chart.setFrame(RectD{100.0, 50.0, 200.0, 200.0});
    chart.tickTree(at(-1.0, -1.0));
    EXPECT_DOUBLE_EQ(chart.sliceAt(0)->arc().center.x, 200.0);
    EXPECT_DOUBLE_EQ(chart.sliceAt(0)->arc().center.y, 150.0);
}

TEST(PieChart, HoveredSliceFollowsPointer) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    // 等值四片：每个象限正好一片（-90°→0° / 0°→90° / 90°→180° / 180°→270°）。
    PieChartData data;
    data.slices.push_back({"Alpha", 25.0});
    data.slices.push_back({"Beta", 25.0});
    data.slices.push_back({"Gamma", 25.0});
    data.slices.push_back({"Delta", 25.0});
    chart.setData(&data);
    chart.tickTree(at(-1.0, -1.0));  // 重建

    // 片 0 是右上象限（-90°→0°），圆心 (100,100)，取角平分线上的点。
    const Vec2d alpha{100.0 + 40.0 / std::sqrt(2.0), 100.0 - 40.0 / std::sqrt(2.0)};
    chart.tickTree(at(alpha.x, alpha.y));
    ASSERT_NE(chart.hoveredSlice(), nullptr);
    EXPECT_EQ(chart.hoveredSlice()->label(), "Alpha");

    // 移到片 2（左下象限 90°→180°）。
    const Vec2d gamma{100.0 - 40.0 / std::sqrt(2.0), 100.0 + 40.0 / std::sqrt(2.0)};
    chart.tickTree(at(gamma.x, gamma.y));
    ASSERT_NE(chart.hoveredSlice(), nullptr);
    EXPECT_EQ(chart.hoveredSlice()->label(), "Gamma");

    chart.tickTree(at(-300.0, -300.0));
    EXPECT_EQ(chart.hoveredSlice(), nullptr);
}

TEST(PieChart, VersionChangeRebuildsSlices) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    PieChartData data = fourSlices();
    chart.setData(&data, 1);
    chart.tickTree(at(-1.0, -1.0));
    chart.tickTree(at(-1.0, -1.0));

    data.slices[0].value = 10.0;
    chart.setData(&data, 2);  // 同一指针，version 变了
    chart.tickTree(at(-1.0, -1.0));

    // 不比较节点指针：重建后分配器常会复用刚释放的块，地址可能相同。
    // 看数据：新节点拿到的是改过的值（40→10，总数 70）。
    EXPECT_DOUBLE_EQ(chart.sliceAt(0)->value(), 10.0);
    EXPECT_NEAR(chart.sliceAt(0)->fraction(), 10.0 / 70.0, 1e-12);
}

TEST(PieChart, NullDataClearsSlices) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    PieChartData data = fourSlices();
    chart.setData(&data);
    chart.tickTree(at(-1.0, -1.0));
    chart.tickTree(at(-1.0, -1.0));
    ASSERT_EQ(chart.sliceCount(), 4u);

    chart.setData(nullptr);
    chart.tickTree(at(-1.0, -1.0));
    EXPECT_EQ(chart.sliceCount(), 0u);
    EXPECT_FALSE(chart.valid());
}

TEST(PieChart, PaletteAppliesOnRebuild) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    PieChartData data = fourSlices();
    chart.setData(&data);
    chart.tickTree(at(-1.0, -1.0));
    chart.tickTree(at(-1.0, -1.0));

    // 换色板 + 触发一次重建，颜色才重新分配。
    chart.setPalette({Color::Hex(0xFF0000)});
    chart.setData(&data, 9);
    chart.tickTree(at(-1.0, -1.0));
    ASSERT_NE(chart.sliceAt(0), nullptr);
    EXPECT_EQ(chart.sliceAt(0)->fillColor(), Color::Hex(0xFF0000));
}

TEST(PieChart, SliceDefaultsApplyOnRebuild) {
    PieChart chart("pie");
    chart.setFrame(RectD{0.0, 0.0, 200.0, 200.0});

    PieChartData data = fourSlices();
    chart.setData(&data);
    chart.tickTree(at(-1.0, -1.0));
    chart.tickTree(at(-1.0, -1.0));

    // 页面配置（explode）走这条通道，重建时铺到每个扇区。
    chart.setSliceDefaults(8.0);
    chart.tickTree(at(-1.0, -1.0));  // setSliceDefaults 标脏 → 重建
    ASSERT_EQ(chart.sliceCount(), 4u);
    for (std::size_t i = 0; i < chart.sliceCount(); ++i) {
        const SceneSlice* slice = chart.sliceAt(i);
        ASSERT_NE(slice, nullptr);
        EXPECT_DOUBLE_EQ(slice->explodeDistance(), 8.0);
    }
}

}  // namespace
