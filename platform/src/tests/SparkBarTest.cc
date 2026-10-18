#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "kiriyama/core/SparkBar.h"
#include "kiriyama/scene/SparkBar.h"
#include "kiriyama/scene/Widget.h"

namespace {

using kiriyama::core::kInvalidIndex;
using kiriyama::core::RectD;
using kiriyama::core::SparkBarData;
using kiriyama::core::SparkBarOptions;
using kiriyama::core::Vec2d;
using kiriyama::scene::FrameContext;
using kiriyama::scene::SparkBar;

[[nodiscard]] FrameContext at(double x, double y) {
    FrameContext ctx;
    ctx.time = 0.0;
    ctx.deltaTime = 0.016;
    ctx.pointer.position = Vec2d{x, y};
    ctx.pointer.down = false;
    return ctx;
}

[[nodiscard]] SparkBarData sample() {
    SparkBarData data;
    data.series = {{"Revenue", {}}, {"Cost", {}}};
    data.tags = {"2022", "2023", "2024"};
    data.values = {
        {10.0, 4.0},
        {15.0, 10.0},
        {20.0, 5.0},
    };
    return data;
}

/// 已建好布局的组件：第一帧重建，第二帧才按新布局判命中。
struct Fixture {
    SparkBarData data = sample();
    SparkBar bar{"spark"};

    Fixture() {
        bar.setFrame(RectD{0.0, 0.0, 300.0, 200.0});
        SparkBarOptions options;
        options.barGap = 10.0;
        options.segmentGap = 1.5;
        bar.setOptions(options);
        bar.setData(data);
        bar.tickTree(at(-1.0, -1.0));
    }

    void moveTo(double x, double y) { bar.tickTree(at(x, y)); }
};

// ---------------------------------------------------------------- 命中

TEST(SparkBar, HitTestFollowsBars) {
    Fixture fixture;

    // 柱 1（2023）的柱体中部：命中。
    fixture.moveTo(150.0, 100.0);
    EXPECT_TRUE(fixture.bar.hovered());

    // 柱 1 与柱 2 之间的空隙（x ≈ 200，柱宽 93.3、间距 10）：不命中。
    fixture.moveTo(201.0, 100.0);
    EXPECT_FALSE(fixture.bar.hovered());

    // 画布上方（柱顶之上，没有柱）：不命中。
    fixture.moveTo(150.0, 250.0);
    EXPECT_FALSE(fixture.bar.hovered());
}

TEST(SparkBar, TagRowIsHitAreaForZeroTotal) {
    SparkBarData data;
    data.series = {{"a", {}}};
    data.tags = {"empty"};
    data.values = {{0.0}};

    SparkBar bar("spark");
    bar.setFrame(RectD{0.0, 0.0, 120.0, 80.0});
    bar.setData(data);
    bar.tickTree(at(-1.0, -1.0));

    // 总量 0：柱高 0，只在 tag 行上命中。
    bar.tickTree(at(60.0, 72.0));
    EXPECT_TRUE(bar.hovered());
}

// ---------------------------------------------------------------- 气泡内容

TEST(SparkBar, TooltipOnSegmentShowsSeriesAndShare) {
    Fixture fixture;
    fixture.moveTo(150.0, 170.0);  // 柱 1 的最底层段（Revenue 15）

    const auto text = fixture.bar.tooltipContent();
    EXPECT_EQ(text.title, "2023 · Revenue");
    EXPECT_EQ(text.body, "15 · 60.0%");  // 15 / 25
}

TEST(SparkBar, TooltipOnTagRowShowsWholeBar) {
    Fixture fixture;
    // 柱 0 的 tag 行（x 在柱 0 内，y 落在 tag 行）。
    fixture.moveTo(40.0, 190.0);

    const auto text = fixture.bar.tooltipContent();
    EXPECT_EQ(text.title, "2022");
    // 整柱汇总：两个 series 的值 + 合计。
    EXPECT_NE(text.body.find("Revenue 10"), std::string::npos);
    EXPECT_NE(text.body.find("Cost 4"), std::string::npos);
    EXPECT_NE(text.body.find("合计 14"), std::string::npos);
}

TEST(SparkBar, StaticContentOverridesGenerated) {
    Fixture fixture;
    fixture.bar.setTooltipContent("自定义", "静态内容");
    fixture.moveTo(150.0, 170.0);

    const auto text = fixture.bar.tooltipContent();
    EXPECT_EQ(text.title, "自定义");
    EXPECT_EQ(text.body, "静态内容");
}

TEST(SparkBar, AnchorIsChartFrame) {
    Fixture fixture;
    fixture.moveTo(150.0, 100.0);

    // 气泡浮在整个图的上方：锚点就是组件 frame，不随柱子位置跳。
    EXPECT_EQ(fixture.bar.tooltipAnchor(), fixture.bar.frame());
}

TEST(SparkBar, HasContentOnlyWithData) {
    SparkBar empty("spark");
    empty.setFrame(RectD{0.0, 0.0, 200.0, 100.0});
    empty.tickTree(at(-1.0, -1.0));
    EXPECT_FALSE(empty.hasTooltipContent());

    Fixture fixture;
    EXPECT_TRUE(fixture.bar.hasTooltipContent());
}

// ---------------------------------------------------------------- 回归：坐标系

/// 组件 frame 带偏移时（真实宿主就是这个形态：{64, 96, ...}），
/// 布局必须平移到节点本地坐标。曾经只有渲染层加了 frame 原点、命中没加，
/// 命中区整体上移 frame.y，导致柱子下半截既没 hover 也没 tooltip。
TEST(SparkBar, HitTestRespectsFrameOrigin) {
    SparkBarData data = sample();
    SparkBar bar("spark");
    bar.setFrame(RectD{64.0, 96.0, 300.0, 200.0});
    bar.setData(data);
    bar.tickTree(at(-1.0, -1.0));  // 第一帧重建布局

    ASSERT_TRUE(bar.valid());
    const RectD barRect = bar.layout().items[0].barRect;
    ASSERT_FALSE(barRect.empty());

    // 柱底内侧：正是旧 bug 漏掉的那一块（上移 96px 后落到命中区外）。
    const Vec2d bottom{barRect.center().x, barRect.bottom() - 2.0};
    bar.tickTree(at(bottom.x, bottom.y));
    EXPECT_TRUE(bar.hovered());
    EXPECT_EQ(bar.hoveredItem(), 0u);
    EXPECT_EQ(bar.hoveredSegment(), 0u);  // 最底层是 series 0

    // 同一根柱的顶部也要能命中（顶段是最后一个 series）。
    const Vec2d top{barRect.center().x, barRect.top() + 2.0};
    bar.tickTree(at(top.x, top.y));
    EXPECT_TRUE(bar.hovered());
    EXPECT_EQ(bar.hoveredSegment(), 1u);

    // 柱底外侧（frame 原点上方那段空白）不该命中，避免"修偏移"修过头。
    bar.tickTree(at(bottom.x, bottom.y - 96.0 - 4.0));
    EXPECT_FALSE(bar.hovered());
}

TEST(SparkBar, TooltipAvailableOnLowerHalfOfBar) {
    SparkBarData data = sample();
    SparkBar bar("spark");
    bar.setFrame(RectD{64.0, 96.0, 300.0, 250.0});
    bar.setData(data);
    bar.tickTree(at(-1.0, -1.0));

    ASSERT_TRUE(bar.valid());
    const RectD barRect = bar.layout().items[0].barRect;
    const Vec2d lowerHalf{barRect.center().x, barRect.bottom() - 4.0};
    bar.tickTree(at(lowerHalf.x, lowerHalf.y));

    const auto text = bar.tooltipContent();
    EXPECT_EQ(text.title, "2022 · Revenue");
    EXPECT_FALSE(text.body.empty());
}

TEST(SparkBar, HoveredSegmentClearsWhenPointerLeaves) {
    Fixture fixture;
    fixture.moveTo(150.0, 170.0);  // 柱 1 最底层段
    EXPECT_EQ(fixture.bar.hoveredItem(), 1u);
    EXPECT_EQ(fixture.bar.hoveredSegment(), 0u);

    fixture.moveTo(-300.0, -300.0);
    EXPECT_EQ(fixture.bar.hoveredItem(), kInvalidIndex);
    EXPECT_EQ(fixture.bar.hoveredSegment(), kInvalidIndex);
}

}  // namespace
