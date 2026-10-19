#include <gtest/gtest.h>

#include "kiriyama/scene/Animation.h"

namespace {

using kiriyama::core::NodeType;
using kiriyama::core::Vec2d;
using kiriyama::math::Ease;
using kiriyama::scene::Animation;
using kiriyama::scene::AnimProperty;
using kiriyama::scene::AnimSpec;

[[nodiscard]] AnimSpec linear(double duration, double delay = 0.0) {
    return AnimSpec{.duration = duration, .ease = Ease::Linear, .delay = delay};
}

// ---------------------------------------------------------------- opacity

TEST(Animation, OpacityRunsFromCurrentToTarget) {
    Animation anim;
    anim.toOpacity(0.4, linear(1.0));

    EXPECT_TRUE(anim.animating(AnimProperty::Opacity));
    EXPECT_NEAR(anim.style().opacity, 1.0, 1e-9);

    anim.update(0.5);
    EXPECT_NEAR(anim.style().opacity, 0.7, 1e-9);

    anim.update(0.5);
    EXPECT_NEAR(anim.style().opacity, 0.4, 1e-9);
    EXPECT_FALSE(anim.animating());
}

TEST(Animation, OpacityIsClamped) {
    Animation anim;
    anim.toOpacity(-3.0, linear(1.0));
    anim.update(1.0);
    EXPECT_NEAR(anim.style().opacity, 0.0, 1e-9);

    anim.toOpacity(9.0, linear(1.0));
    anim.update(1.0);
    EXPECT_NEAR(anim.style().opacity, 1.0, 1e-9);
}

TEST(Animation, AlreadyAtTargetIsNoOpAndDoesNotFireDone) {
    Animation anim;
    int doneCount = 0;
    anim.toOpacity(1.0, AnimSpec{.duration = 1.0, .done = [&] { ++doneCount; }});

    EXPECT_FALSE(anim.animating());
    EXPECT_EQ(doneCount, 0);  // 值没变就不该报完成，否则每帧请求会刷屏
}

// ---------------------------------------------------------------- 中断

TEST(Animation, RepeatRequestDoesNotRestart) {
    Animation anim;
    anim.toOpacity(0.0, linear(1.0));
    anim.update(0.4);

    // 每帧都请求同一个目标：必须被忽略，进度不能被打回 0
    for (int i = 0; i < 10; ++i) {
        anim.toOpacity(0.0, linear(1.0));
    }
    EXPECT_NEAR(anim.progress(AnimProperty::Opacity), 0.4, 1e-9);

    anim.update(0.6);
    EXPECT_NEAR(anim.style().opacity, 0.0, 1e-9);
}

TEST(Animation, NewTargetContinuesFromCurrentValue) {
    Animation anim;
    anim.toOpacity(0.0, linear(1.0));
    anim.update(0.5);
    ASSERT_NEAR(anim.style().opacity, 0.5, 1e-9);

    // 掉头：从 0.5 接着往 1 走，不能跳回起点
    anim.toOpacity(1.0, linear(1.0));
    EXPECT_NEAR(anim.style().opacity, 0.5, 1e-9);

    anim.update(0.5);
    EXPECT_NEAR(anim.style().opacity, 0.75, 1e-9);
}

TEST(Animation, OnlyOneTrackPerProperty) {
    Animation anim;
    anim.toOpacity(0.0, linear(1.0));
    anim.toOpacity(0.0, AnimSpec{.duration = 2.0, .ease = Ease::Linear});
    EXPECT_EQ(anim.trackCount(), 1u);  // 换参数是替换，不是叠加
}

TEST(Animation, StopKeepsCurrentValueAndSkipsCallback) {
    Animation anim;
    int doneCount = 0;
    anim.toOpacity(0.0, AnimSpec{.duration = 1.0, .ease = Ease::Linear,
                                 .done = [&] { ++doneCount; }});
    anim.update(0.3);

    anim.stop(AnimProperty::Opacity);
    EXPECT_NEAR(anim.style().opacity, 0.7, 1e-9);
    EXPECT_FALSE(anim.animating());

    anim.update(1.0);
    EXPECT_NEAR(anim.style().opacity, 0.7, 1e-9);
    EXPECT_EQ(doneCount, 0);
}

TEST(Animation, FinishJumpsToTargetWithoutCallback) {
    Animation anim;
    int doneCount = 0;
    anim.toOpacity(0.2, AnimSpec{.duration = 1.0, .ease = Ease::Linear,
                                 .done = [&] { ++doneCount; }});
    anim.update(0.2);

    anim.finish(AnimProperty::Opacity);
    EXPECT_NEAR(anim.style().opacity, 0.2, 1e-9);
    EXPECT_FALSE(anim.animating());
    EXPECT_EQ(doneCount, 0);
}

// ---------------------------------------------------------------- 完成回调

TEST(Animation, DoneFiresExactlyOnce) {
    Animation anim;
    int doneCount = 0;
    anim.toOpacity(0.0, AnimSpec{.duration = 0.5, .ease = Ease::Linear,
                                 .done = [&] { ++doneCount; }});

    anim.update(0.5);
    EXPECT_EQ(doneCount, 1);
    anim.update(0.5);
    anim.update(0.5);
    EXPECT_EQ(doneCount, 1);
}

TEST(Animation, DoneCallbackCanStartAnotherTrack) {
    Animation anim;
    anim.toOpacity(0.0, AnimSpec{.duration = 0.5, .ease = Ease::Linear, .done = [&] {
                                     anim.toOpacity(1.0, linear(0.5));
                                 }});

    anim.update(0.5);
    ASSERT_NEAR(anim.style().opacity, 0.0, 1e-9);
    EXPECT_TRUE(anim.animating(AnimProperty::Opacity));

    anim.update(0.5);
    EXPECT_NEAR(anim.style().opacity, 1.0, 1e-9);
}

// ---------------------------------------------------------------- 位移 / 缩放

TEST(Animation, TranslationMovesNode) {
    Animation anim;
    anim.toTranslation({100.0, 50.0}, linear(1.0));

    anim.update(0.5);
    EXPECT_NEAR(anim.transform().m02, 50.0, 1e-9);
    EXPECT_NEAR(anim.transform().m12, 25.0, 1e-9);

    anim.update(0.5);
    EXPECT_NEAR(anim.transform().m02, 100.0, 1e-9);
    EXPECT_NEAR(anim.transform().m12, 50.0, 1e-9);
}

TEST(Animation, ByTranslationIsRelative) {
    Animation anim;
    anim.setTranslation({10.0, 10.0});
    anim.byTranslation({5.0, -5.0}, linear(1.0));
    anim.update(1.0);

    EXPECT_NEAR(anim.transform().m02, 15.0, 1e-9);
    EXPECT_NEAR(anim.transform().m12, 5.0, 1e-9);
}

TEST(Animation, ScaleKeepsTranslation) {
    Animation anim;
    anim.setTranslation({30.0, 40.0});
    anim.toScale({2.0, 2.0}, linear(1.0));

    anim.update(0.5);
    EXPECT_NEAR(anim.transform().m00, 1.5, 1e-9);
    EXPECT_NEAR(anim.transform().m11, 1.5, 1e-9);
    EXPECT_NEAR(anim.transform().m02, 30.0, 1e-9);
    EXPECT_NEAR(anim.transform().m12, 40.0, 1e-9);
}

// ---------------------------------------------------------------- 时序

TEST(Animation, DelayHoldsValueFirst) {
    Animation anim;
    anim.toOpacity(0.0, linear(1.0, /*delay=*/0.5));

    anim.update(0.25);
    EXPECT_NEAR(anim.style().opacity, 1.0, 1e-9);
    EXPECT_NEAR(anim.progress(AnimProperty::Opacity), 0.0, 1e-9);

    anim.update(0.25);  // 刚好走完 delay
    EXPECT_NEAR(anim.style().opacity, 1.0, 1e-9);

    anim.update(0.5);
    EXPECT_NEAR(anim.style().opacity, 0.5, 1e-9);
}

TEST(Animation, ZeroDurationAppliesImmediately) {
    Animation anim;
    int doneCount = 0;
    anim.toOpacity(0.25, AnimSpec{.duration = 0.0, .done = [&] { ++doneCount; }});

    EXPECT_NEAR(anim.style().opacity, 0.25, 1e-9);
    EXPECT_FALSE(anim.animating());
    EXPECT_EQ(doneCount, 1);
}

TEST(Animation, DefaultDurationIsUsedWhenSpecOmitsIt) {
    Animation anim;
    anim.setDefaultDuration(0.25);
    anim.toOpacity(0.0);

    anim.update(0.2);
    EXPECT_TRUE(anim.animating());
    anim.update(0.05);
    EXPECT_FALSE(anim.animating());
    EXPECT_NEAR(anim.style().opacity, 0.0, 1e-9);
}

TEST(Animation, ZeroDeltaChangesNothing) {
    Animation anim;
    anim.toOpacity(0.0, linear(1.0));
    EXPECT_FALSE(anim.update(0.0));
    EXPECT_NEAR(anim.style().opacity, 1.0, 1e-9);
}

// ---------------------------------------------------------------- 多轨道

TEST(Animation, PropertiesRunIndependently) {
    Animation anim;
    anim.toOpacity(0.0, linear(1.0));
    anim.toTranslation({40.0, 0.0}, linear(1.0));

    anim.update(0.5);
    EXPECT_NEAR(anim.style().opacity, 0.5, 1e-9);
    EXPECT_NEAR(anim.transform().m02, 20.0, 1e-9);

    // 只停一条，另一条继续
    anim.stop(AnimProperty::Opacity);
    EXPECT_TRUE(anim.animating(AnimProperty::Translation));
    anim.update(0.5);
    EXPECT_NEAR(anim.transform().m02, 40.0, 1e-9);
}

TEST(Animation, StopAllClearsEverything) {
    Animation anim;
    anim.toOpacity(0.0, linear(1.0));
    anim.toTranslation({10.0, 0.0}, linear(1.0));
    anim.update(0.5);

    anim.stopAll();
    EXPECT_FALSE(anim.animating());
    EXPECT_NEAR(anim.style().opacity, 0.5, 1e-9);
}

TEST(Animation, ProgressWithoutTrackIsOne) {
    Animation anim;
    EXPECT_NEAR(anim.progress(AnimProperty::Scale), 1.0, 1e-9);
}

// ---------------------------------------------------------------- 边框与阴影过渡

TEST(Animation, BorderAndCornerRadiusTransition) {
    Animation anim;
    anim.setBorder(kiriyama::core::Color::Rgb(0.0, 0.0, 0.0), 1.0, 4.0);

    int doneCount = 0;
    anim.toBorder(kiriyama::core::Color::Rgb(1.0, 0.5, 0.0), 5.0,
                  AnimSpec{.duration = 0.4, .ease = Ease::Linear, .done = [&] { ++doneCount; }});
    anim.toCornerRadius(12.0, linear(0.4));

    EXPECT_TRUE(anim.animating(AnimProperty::BorderWidth));
    EXPECT_TRUE(anim.animating(AnimProperty::BorderColor));
    EXPECT_TRUE(anim.animating(AnimProperty::CornerRadius));

    anim.update(0.2);
    EXPECT_NEAR(anim.style().strokeWidth, 3.0, 1e-9);
    EXPECT_NEAR(anim.style().stroke.color.r, 0.5, 1e-6);
    EXPECT_NEAR(anim.style().stroke.color.g, 0.25, 1e-6);
    EXPECT_NEAR(anim.style().cornerRadius, 8.0, 1e-9);
    EXPECT_EQ(doneCount, 0);

    anim.update(0.2);
    EXPECT_NEAR(anim.style().strokeWidth, 5.0, 1e-9);
    EXPECT_NEAR(anim.style().stroke.color.r, 1.0, 1e-6);
    EXPECT_NEAR(anim.style().stroke.color.g, 0.5, 1e-6);
    EXPECT_NEAR(anim.style().cornerRadius, 12.0, 1e-9);
    EXPECT_EQ(doneCount, 1);
    EXPECT_FALSE(anim.animating());
}

TEST(Animation, ShadowTransitionWithDuration) {
    Animation anim;
    anim.setShadow(kiriyama::core::Color::Rgba(0.0, 0.0, 0.0, 0.0), Vec2d{0.0, 0.0}, 0.0, 0.0);

    int doneCount = 0;
    kiriyama::core::BoxShadow target;
    target.color = kiriyama::core::Color::Rgba(0.0, 0.0, 0.0, 0.4);
    target.offset = Vec2d{0.0, 8.0};
    target.blur = 16.0;
    target.spread = 2.0;

    anim.toShadow(target,
                  AnimSpec{.duration = 0.5, .ease = Ease::Linear, .done = [&] { ++doneCount; }});

    anim.update(0.25);
    EXPECT_NEAR(anim.shadow().color.color.a, 0.2, 1e-6);
    EXPECT_NEAR(anim.shadow().offset.y, 4.0, 1e-9);
    EXPECT_NEAR(anim.shadow().blur, 8.0, 1e-9);
    EXPECT_NEAR(anim.shadow().spread, 1.0, 1e-9);
    EXPECT_TRUE(anim.style().hasShadow());

    anim.update(0.25);
    EXPECT_NEAR(anim.shadow().color.color.a, 0.4, 1e-6);
    EXPECT_NEAR(anim.shadow().offset.y, 8.0, 1e-9);
    EXPECT_NEAR(anim.shadow().blur, 16.0, 1e-9);
    EXPECT_NEAR(anim.shadow().spread, 2.0, 1e-9);
    EXPECT_EQ(doneCount, 1);
}

}  // namespace
