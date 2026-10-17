#include <gtest/gtest.h>

#include "kiriyama/core/Tooltip.h"

namespace {

using kiriyama::core::computeTooltipLayout;
using kiriyama::core::RectD;
using kiriyama::core::TooltipAlign;
using kiriyama::core::TooltipLayout;
using kiriyama::core::TooltipOptions;
using kiriyama::core::TooltipSide;
using kiriyama::core::TooltipState;
using kiriyama::core::TooltipTiming;
using kiriyama::core::Vec2d;

constexpr RectD kViewport{0.0, 0.0, 800.0, 600.0};

[[nodiscard]] TooltipOptions baseOptions() {
    TooltipOptions options;
    options.gap = 8.0;
    options.margin = 4.0;
    options.arrowWidth = 10.0;
    options.cornerRadius = 6.0;
    return options;
}

// ---------------------------------------------------------------- 定位

TEST(TooltipLayout, AboveCenteredByDefault) {
    const RectD anchor{100.0, 100.0, 40.0, 20.0};
    const TooltipLayout layout = computeTooltipLayout(anchor, {100.0, 40.0}, kViewport, baseOptions());

    ASSERT_TRUE(layout.visible);
    EXPECT_EQ(layout.side, TooltipSide::Above);
    EXPECT_FALSE(layout.flipped);

    // 水平居中于锚点中心 (120)，垂直贴在锚点上方 8px
    EXPECT_DOUBLE_EQ(layout.rect.x, 70.0);
    EXPECT_DOUBLE_EQ(layout.rect.y, 52.0);
    EXPECT_DOUBLE_EQ(layout.rect.width, 100.0);
    EXPECT_DOUBLE_EQ(layout.rect.height, 40.0);

    ASSERT_TRUE(layout.hasArrow);
    EXPECT_DOUBLE_EQ(layout.arrowBase.x, 120.0);
    EXPECT_DOUBLE_EQ(layout.arrowBase.y, 92.0);   // 卡片下边
    EXPECT_DOUBLE_EQ(layout.arrowTip.y, 100.0);   // 锚点上边
}

TEST(TooltipLayout, FlipsWhenNoRoomAbove) {
    const RectD anchor{100.0, 2.0, 40.0, 20.0};
    const TooltipLayout layout = computeTooltipLayout(anchor, {100.0, 40.0}, kViewport, baseOptions());

    ASSERT_TRUE(layout.visible);
    EXPECT_EQ(layout.side, TooltipSide::Below);
    EXPECT_TRUE(layout.flipped);
    EXPECT_DOUBLE_EQ(layout.rect.y, 30.0);  // 22 + gap
    EXPECT_TRUE(layout.hasArrow);
}

TEST(TooltipLayout, FlipsLeftRight) {
    const RectD anchor{4.0, 200.0, 30.0, 30.0};

    TooltipOptions options = baseOptions();
    options.side = TooltipSide::Left;
    options.align = TooltipAlign::Center;

    const TooltipLayout layout = computeTooltipLayout(anchor, {120.0, 40.0}, kViewport, options);
    ASSERT_TRUE(layout.visible);
    EXPECT_EQ(layout.side, TooltipSide::Right);
    EXPECT_TRUE(layout.flipped);
    EXPECT_DOUBLE_EQ(layout.rect.x, 42.0);  // 34 + gap
}

TEST(TooltipLayout, ClampsToViewportWhenNeitherSideFits) {
    const RectD anchor{760.0, 300.0, 40.0, 20.0};
    const TooltipLayout layout = computeTooltipLayout(anchor, {120.0, 40.0}, kViewport, baseOptions());

    ASSERT_TRUE(layout.visible);
    EXPECT_TRUE(layout.clamped);
    EXPECT_LE(layout.rect.right(), kViewport.right() - 4.0 + 1e-9);
    EXPECT_GE(layout.rect.left(), 4.0 - 1e-9);
}

TEST(TooltipLayout, AlignStartAndEnd) {
    const RectD anchor{100.0, 100.0, 60.0, 20.0};

    TooltipOptions start = baseOptions();
    start.align = TooltipAlign::Start;
    EXPECT_DOUBLE_EQ(computeTooltipLayout(anchor, {80.0, 30.0}, kViewport, start).rect.x, 100.0);

    TooltipOptions end = baseOptions();
    end.align = TooltipAlign::End;
    EXPECT_DOUBLE_EQ(computeTooltipLayout(anchor, {80.0, 30.0}, kViewport, end).rect.x, 80.0);
}

TEST(TooltipLayout, OffsetShiftsAlongEdge) {
    const RectD anchor{100.0, 100.0, 40.0, 20.0};
    TooltipOptions options = baseOptions();
    options.align = TooltipAlign::Start;
    options.offset = 12.0;

    const TooltipLayout layout = computeTooltipLayout(anchor, {80.0, 30.0}, kViewport, options);
    EXPECT_DOUBLE_EQ(layout.rect.x, 112.0);
    // 箭头跟着锚点中心走，不该被 offset 影响（offset 是整块的位移，不是箭头）
    EXPECT_DOUBLE_EQ(layout.arrowTip.x, 120.0);
}

TEST(TooltipLayout, ArrowClampedNearCorner) {
    const RectD anchor{0.0, 100.0, 8.0, 20.0};  // 锚点贴着左边缘
    const TooltipLayout layout = computeTooltipLayout(anchor, {200.0, 40.0}, kViewport, baseOptions());

    ASSERT_TRUE(layout.hasArrow);
    // 卡片被夹到 x=4，锚点中心 4 落在圆角区里，箭头要往里推到 4+6
    EXPECT_TRUE(layout.arrowClamped);
    EXPECT_NEAR(layout.arrowTip.x, 10.0, 1e-9);
}

TEST(TooltipLayout, ArrowDisappearsWhenCardOverlapsAnchor) {
    const RectD anchor{10.0, 10.0, 10.0, 10.0};
    // 比视口还高的内容：无论贴哪一侧都会被夹回来盖住锚点
    const TooltipLayout layout = computeTooltipLayout(anchor, {300.0, 700.0}, kViewport, baseOptions());

    ASSERT_TRUE(layout.visible);
    EXPECT_TRUE(layout.clamped);
    EXPECT_FALSE(layout.hasArrow);  // 箭头会往回指，干脆不画
}

TEST(TooltipLayout, EmptyContentIsInvisible) {
    const TooltipLayout layout =
        computeTooltipLayout(RectD{10.0, 10.0, 20.0, 20.0}, {0.0, 0.0}, kViewport, baseOptions());
    EXPECT_FALSE(layout.visible);
}

TEST(TooltipLayout, ArrowPolygonPointsOutward) {
    const RectD anchor{100.0, 100.0, 40.0, 20.0};
    const TooltipLayout layout = computeTooltipLayout(anchor, {100.0, 40.0}, kViewport, baseOptions());

    Vec2d points[3]{};
    ASSERT_TRUE(kiriyama::core::tooltipArrowPolygon(layout, 10.0, points));

    EXPECT_DOUBLE_EQ(points[0].x, 120.0);  // tip
    EXPECT_DOUBLE_EQ(points[0].y, 100.0);
    EXPECT_DOUBLE_EQ(points[1].x, 115.0);
    EXPECT_DOUBLE_EQ(points[1].y, 92.0);
    EXPECT_DOUBLE_EQ(points[2].x, 125.0);
    EXPECT_DOUBLE_EQ(points[2].y, 92.0);
}

// ---------------------------------------------------------------- 状态机

namespace {

[[nodiscard]] TooltipTiming testTiming() {
    TooltipTiming timing;
    timing.showDelay = 300.0;
    timing.hideDelay = 80.0;
    timing.focusDelay = 0.0;
    timing.fadeIn = 100.0;
    return timing;
}

constexpr RectD kAnchor{10.0, 10.0, 50.0, 20.0};

}  // namespace

TEST(TooltipState, HoverWaitsForShowDelay) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);

    EXPECT_FALSE(state.update(0.0).changed);
    EXPECT_FALSE(state.update(299.0).changed);
    EXPECT_FALSE(state.visible());

    const auto change = state.update(300.0);
    EXPECT_TRUE(change.changed);
    EXPECT_TRUE(change.visible);
    EXPECT_EQ(change.targetId, 1u);
    EXPECT_TRUE(state.visible());
}

TEST(TooltipState, RepeatedHoverDoesNotRestartTimer) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    state.hover(1, kAnchor, 250.0);  // 指针在同一个目标上挪动

    EXPECT_FALSE(state.update(299.0).visible);
    EXPECT_TRUE(state.update(300.0).visible);
}

TEST(TooltipState, SwitchingTargetRestartsDelay) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    ASSERT_TRUE(state.update(300.0).visible);

    state.hover(2, kAnchor, 400.0);
    EXPECT_FALSE(state.update(400.0).changed);   // 新目标要重新等
    EXPECT_EQ(state.targetId(), 1u);             // 旧气泡还挂着
    EXPECT_TRUE(state.update(700.0).changed);
    EXPECT_EQ(state.targetId(), 2u);
}

TEST(TooltipState, HidesAfterHideDelay) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    ASSERT_TRUE(state.update(300.0).visible);

    state.hover(0, kAnchor, 1000.0);  // 离开
    EXPECT_FALSE(state.update(1079.0).changed);
    EXPECT_TRUE(state.visible());

    const auto change = state.update(1080.0);
    EXPECT_TRUE(change.changed);
    EXPECT_FALSE(change.visible);
    EXPECT_FALSE(state.visible());
}

TEST(TooltipState, FocusShowsImmediately) {
    TooltipState state{testTiming()};
    state.focus(7, kAnchor, 500.0);

    const auto change = state.update(500.0);
    EXPECT_TRUE(change.visible);
    EXPECT_EQ(change.targetId, 7u);
}

TEST(TooltipState, FocusOutranksHover) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    ASSERT_TRUE(state.update(300.0).visible);

    state.focus(9, kAnchor, 400.0);
    const auto change = state.update(400.0);
    EXPECT_TRUE(change.changed);
    EXPECT_EQ(state.targetId(), 9u);
}

TEST(TooltipState, BlurFallsBackToHover) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    state.focus(9, kAnchor, 0.0);
    ASSERT_EQ(state.update(10.0).targetId, 9u);

    state.blur(1000.0);
    // 焦点走了，hover 还在 → 立刻回到 hover 目标，不走延迟
    const auto change = state.update(1001.0);
    EXPECT_TRUE(change.changed);
    EXPECT_EQ(state.targetId(), 1u);
}

TEST(TooltipState, PressHidesImmediately) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    ASSERT_TRUE(state.update(300.0).visible);

    const auto change = state.press(320.0);
    EXPECT_TRUE(change.changed);
    EXPECT_FALSE(change.visible);
    EXPECT_FALSE(state.visible());

    // 手指还压在同一个目标上也不会再弹：hover 已被取消
    EXPECT_FALSE(state.update(900.0).changed);
}

TEST(TooltipState, HideCancelsHover) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    ASSERT_TRUE(state.update(300.0).visible);

    EXPECT_TRUE(state.hide().changed);
    EXPECT_FALSE(state.visible());
    EXPECT_FALSE(state.update(1000.0).changed);  // 没有 hover 了，不会自己回来
}

TEST(TooltipState, OpacityFadesIn) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    ASSERT_TRUE(state.update(300.0).visible);

    EXPECT_NEAR(state.opacity(300.0), 0.0, 1e-9);
    EXPECT_NEAR(state.opacity(350.0), 0.5, 1e-9);
    EXPECT_NEAR(state.opacity(400.0), 1.0, 1e-9);
    EXPECT_NEAR(state.opacity(9999.0), 1.0, 1e-9);
}

TEST(TooltipState, AnchorFollowsTarget) {
    TooltipState state{testTiming()};
    state.hover(1, RectD{0.0, 0.0, 10.0, 10.0}, 0.0);
    ASSERT_TRUE(state.update(300.0).visible);

    // 场景滚动 / 重排后锚点变了，同一 id 也要跟着走
    state.hover(1, RectD{40.0, 50.0, 10.0, 10.0}, 310.0);
    (void)state.update(310.0);
    EXPECT_DOUBLE_EQ(state.anchor().x, 40.0);
    EXPECT_DOUBLE_EQ(state.anchor().y, 50.0);
}

TEST(TooltipState, ResetClearsEverything) {
    TooltipState state{testTiming()};
    state.hover(1, kAnchor, 0.0);
    state.focus(2, kAnchor, 0.0);
    ASSERT_TRUE(state.update(10.0).visible);

    state.reset();
    EXPECT_FALSE(state.visible());
    EXPECT_FALSE(state.focused());
    EXPECT_FALSE(state.update(1000.0).changed);
}

}  // namespace
