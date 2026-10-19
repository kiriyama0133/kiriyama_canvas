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
    ASSERT_TRUE(fixture.bar.valid());
    ASSERT_EQ(fixture.bar.layout().items.size(), 3u);

    const RectD row1Bar = fixture.bar.layout().items[1].barRect;
    // 第 1 行（2023）条形中部：命中。
    fixture.moveTo(row1Bar.center().x, row1Bar.center().y);
    EXPECT_TRUE(fixture.bar.hovered());

    // 第 0 行条底与第 1 行 tag 顶之间的空隙：不命中。
    const double gapY = (fixture.bar.layout().items[0].barRect.bottom() +
                         fixture.bar.layout().items[1].tagRect.top()) *
                        0.5;
    fixture.moveTo(150.0, gapY);
    EXPECT_FALSE(fixture.bar.hovered());

    // 画布下方越界：不命中。
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

    ASSERT_EQ(bar.layout().items.size(), 1u);
    const Vec2d tagCenter = bar.layout().items[0].tagRect.center();
    // 总量 0：只在 tag 行上命中。
    bar.tickTree(at(tagCenter.x, tagCenter.y));
    EXPECT_TRUE(bar.hovered());
}

// ---------------------------------------------------------------- 气泡内容

TEST(SparkBar, TooltipOnSegmentShowsSeriesAndShare) {
    Fixture fixture;
    const Vec2d seg0Center = fixture.bar.layout().items[1].segments[0].rect.center();
    fixture.moveTo(seg0Center.x, seg0Center.y);  // 第 1 行（2023）左起第 0 段（Revenue 15）

    const auto text = fixture.bar.tooltipContent();
    EXPECT_EQ(text.title, "2023 · Revenue");
    EXPECT_EQ(text.body, "15 · 60.0%");  // 15 / 25
}

TEST(SparkBar, TooltipOnTagRowShowsWholeBar) {
    Fixture fixture;
    const Vec2d tag0Center = fixture.bar.layout().items[0].tagRect.center();
    fixture.moveTo(tag0Center.x, tag0Center.y);

    const auto text = fixture.bar.tooltipContent();
    EXPECT_EQ(text.title, "2022");
    // 整行汇总：两个 series 的值 + 合计。
    EXPECT_NE(text.body.find("Revenue 10"), std::string::npos);
    EXPECT_NE(text.body.find("Cost 4"), std::string::npos);
    EXPECT_NE(text.body.find("合计 14"), std::string::npos);
}

TEST(SparkBar, StaticContentOverridesGenerated) {
    Fixture fixture;
    fixture.bar.setTooltipContent("自定义", "静态内容");
    const Vec2d seg0Center = fixture.bar.layout().items[1].segments[0].rect.center();
    fixture.moveTo(seg0Center.x, seg0Center.y);

    const auto text = fixture.bar.tooltipContent();
    EXPECT_EQ(text.title, "自定义");
    EXPECT_EQ(text.body, "静态内容");
}

TEST(SparkBar, AnchorIsChartFrame) {
    Fixture fixture;
    const Vec2d seg0Center = fixture.bar.layout().items[1].segments[0].rect.center();
    fixture.moveTo(seg0Center.x, seg0Center.y);

    // 气泡浮在整个图的上方：锚点就是组件命中外框，不随条子位置跳。
    EXPECT_EQ(fixture.bar.tooltipAnchor(), fixture.bar.hitBounds());
}

TEST(SparkBar, HasContentOnlyWithData) {
    SparkBar empty("spark");
    empty.setFrame(RectD{0.0, 0.0, 200.0, 100.0});
    empty.tickTree(at(-1.0, -1.0));
    EXPECT_FALSE(empty.hasTooltipContent());

    Fixture fixture;
    EXPECT_TRUE(fixture.bar.hasTooltipContent());
}

// ---------------------------------------------------------------- 回归：坐标系与描述树

[[nodiscard]] const kiriyama::core::SceneNode* findKey(
    const kiriyama::core::SceneNode& node, const std::string& key) {
    if (node.key() == key) {
        return &node;
    }
    for (const auto& child : node.children()) {
        if (const auto* hit = findKey(*child, key)) {
            return hit;
        }
    }
    return nullptr;
}

/// 组件 frame 带偏移时（真实宿主就是这个形态：{64, 96, ...}），
/// 偏移收敛到自身 transform_，frame_ 保持局部尺寸 {0, 0, w, h}，世界坐标命中正常。
TEST(SparkBar, HitTestRespectsFrameOrigin) {
    SparkBarData data = sample();
    SparkBar bar("spark");
    bar.setFrame(RectD{64.0, 96.0, 300.0, 200.0});
    bar.setData(data);
    bar.tickTree(at(-1.0, -1.0));  // 第一帧收敛偏移并重建布局

    ASSERT_TRUE(bar.valid());
    EXPECT_EQ(bar.toWorld({0.0, 0.0}), (Vec2d{64.0, 96.0}));
    EXPECT_EQ(bar.frame(), (RectD{0.0, 0.0, 300.0, 200.0}));

    const auto& segments = bar.layout().items[0].segments;
    ASSERT_EQ(segments.size(), 2u);

    // 左起第一段（世界坐标 = bar.toWorld(局部坐标)）。
    const Vec2d leftSeg = bar.toWorld(segments[0].rect.center());
    bar.tickTree(at(leftSeg.x, leftSeg.y));
    EXPECT_TRUE(bar.hovered());
    EXPECT_EQ(bar.hoveredItem(), 0u);
    EXPECT_EQ(bar.hoveredSegment(), 0u);

    // 同一行的最右段（最后一个 series）。
    const Vec2d rightSeg = bar.toWorld(segments[1].rect.center());
    bar.tickTree(at(rightSeg.x, rightSeg.y));
    EXPECT_TRUE(bar.hovered());
    EXPECT_EQ(bar.hoveredSegment(), 1u);

    // 未加偏移的局部坐标落在组件左上方空白，不该命中。
    bar.tickTree(at(segments[0].rect.center().x, segments[0].rect.center().y));
    EXPECT_FALSE(bar.hovered());
}

TEST(SparkBar, ReconcilesDescriptionIntoInnerSceneTree) {
    Fixture fixture;
    ASSERT_EQ(fixture.bar.children().size(), 1u);
    const auto* inner = fixture.bar.children().front().get();
    EXPECT_NE(findKey(*inner, "row.0.tag"), nullptr);
    EXPECT_NE(findKey(*inner, "row.0.seg.0"), nullptr);
    EXPECT_NE(findKey(*inner, "legend.dot.0"), nullptr);
    EXPECT_NE(findKey(*inner, "legend.text.0"), nullptr);
}

TEST(SparkBar, HoveredSegmentClearsWhenPointerLeaves) {
    Fixture fixture;
    const Vec2d seg0Center = fixture.bar.layout().items[1].segments[0].rect.center();
    fixture.moveTo(seg0Center.x, seg0Center.y);
    EXPECT_EQ(fixture.bar.hoveredItem(), 1u);
    EXPECT_EQ(fixture.bar.hoveredSegment(), 0u);

    fixture.moveTo(-300.0, -300.0);
    EXPECT_EQ(fixture.bar.hoveredItem(), kInvalidIndex);
    EXPECT_EQ(fixture.bar.hoveredSegment(), kInvalidIndex);
}

}  // namespace
