#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "kiriyama/math/Spring.h"

namespace {

using namespace kiriyama::math;

// ω₀ = 1 的三种阻尼：mass = 1, stiffness = 1，临界阻尼 c = 2√(km) = 2
SpringParams makeParams(double damping) {
    SpringParams params;
    params.mass = 1.0;
    params.stiffness = 1.0;
    params.damping = damping;
    return params;
}

TEST(SpringParams, DerivedValues) {
    const SpringParams critical = makeParams(2.0);
    EXPECT_DOUBLE_EQ(critical.angularFrequency(), 1.0);
    EXPECT_DOUBLE_EQ(critical.dampingRatio(), 1.0);
    EXPECT_EQ(critical.classification(), SpringDamping::CriticallyDamped);
    EXPECT_TRUE(critical.usable());

    EXPECT_EQ(makeParams(1.0).classification(), SpringDamping::Underdamped);
    EXPECT_DOUBLE_EQ(makeParams(1.0).dampingRatio(), 0.5);

    EXPECT_EQ(makeParams(4.0).classification(), SpringDamping::Overdamped);
    EXPECT_DOUBLE_EQ(makeParams(4.0).dampingRatio(), 2.0);

    // 无阻尼：ζ = 0，永远不静止
    EXPECT_DOUBLE_EQ(makeParams(0.0).dampingRatio(), 0.0);
}

TEST(SpringParams, FromFrequency) {
    const SpringParams params = SpringParams::FromFrequency(2.0, 0.5);
    EXPECT_NEAR(params.angularFrequency(), 2.0 * kTwoPi, 1e-9);
    EXPECT_NEAR(params.dampingRatio(), 0.5, 1e-12);
    EXPECT_EQ(params.classification(), SpringDamping::Underdamped);
    EXPECT_TRUE(params.usable());
}

TEST(SpringParams, Invalid) {
    SpringParams broken;
    broken.stiffness = 0.0;
    EXPECT_FALSE(broken.usable());

    // 非法参数下推进原样返回，不产生 NaN
    const SpringState state = springAdvance(SpringState{1.0, 2.0}, broken, 0.016);
    EXPECT_DOUBLE_EQ(state.displacement, 1.0);
    EXPECT_DOUBLE_EQ(state.velocity, 2.0);
}

TEST(SpringPhysics, CriticalDampingClosedForm) {
    // ζ = 1, d0 = 1, v0 = 0 时解析解为 (1 + t)·e^(-t)
    const SpringParams params = makeParams(2.0);
    const SpringState start{1.0, 0.0};

    for (double t : {0.1, 0.5, 1.0, 3.0}) {
        const SpringState state = springAdvance(start, params, t);
        const double expected = (1.0 + t) * std::exp(-t);
        EXPECT_NEAR(state.displacement, expected, 1e-12) << "t=" << t;
    }

    // 速度同上：v(t) = -t·e^(-t)
    const double velocity = springAdvance(start, params, 1.0).velocity;
    EXPECT_NEAR(velocity, -std::exp(-1.0), 1e-12);
}

TEST(SpringPhysics, CriticalDoesNotOvershoot) {
    const SpringParams params = makeParams(2.0);
    SpringState state{1.0, 0.0};
    double previous = state.displacement;

    // ω₀ = 1 的临界阻尼要 ~10 s 才衰减到 1e-3 以下
    for (int i = 0; i < 1200; ++i) {
        state = springAdvance(state, params, 1.0 / 120.0);
        EXPECT_GE(state.displacement, 0.0);               // 不越过目标
        EXPECT_LE(state.displacement, previous + 1e-15);  // 单调收敛
        previous = state.displacement;
    }
    EXPECT_LT(state.displacement, 1e-3);
}

TEST(SpringPhysics, UnderdampedOvershootsThenSettles) {
    const SpringParams params = makeParams(1.0);  // ζ = 0.5
    SpringState state{1.0, 0.0};

    // ω_d = √(1-ζ²) ≈ 0.866 rad/s，一个周期约 7.25 s
    const int stepsPerPeriod = 870;
    double firstPeriodPeak = 0.0;
    double lastPeriodPeak = 0.0;
    bool crossed = false;

    for (int i = 0; i < stepsPerPeriod * 3; ++i) {
        state = springAdvance(state, params, 1.0 / 120.0);
        if (state.displacement < 0.0) {
            crossed = true;
        }
        const double amplitude = std::abs(state.displacement);
        if (i < stepsPerPeriod) {
            firstPeriodPeak = std::max(firstPeriodPeak, amplitude);
        }
        if (i >= stepsPerPeriod * 2) {
            lastPeriodPeak = std::max(lastPeriodPeak, amplitude);
        }
    }

    EXPECT_TRUE(crossed);                                 // 会过冲、来回摆
    EXPECT_LT(lastPeriodPeak, firstPeriodPeak * 0.3);     // 包络在衰减
    EXPECT_NEAR(state.displacement, 0.0, 1e-3);
}

TEST(SpringPhysics, OverdampedNeverCrosses) {
    const SpringParams params = makeParams(4.0);  // ζ = 2
    SpringState state{1.0, 0.0};

    // 慢极点衰减率只有 0.268，30 s 才降到 1e-3 以下
    for (int i = 0; i < 3600; ++i) {
        state = springAdvance(state, params, 1.0 / 120.0);
        EXPECT_GE(state.displacement, 0.0);
    }
    EXPECT_LT(state.displacement, 1e-3);

    // 同样时刻，过阻尼比临界阻尼收敛更慢
    const SpringParams critical = makeParams(2.0);
    const double over = springAdvance(SpringState{1.0, 0.0}, params, 0.5).displacement;
    const double crit = springAdvance(SpringState{1.0, 0.0}, critical, 0.5).displacement;
    EXPECT_GT(over, crit);
}

TEST(SpringPhysics, StepComposition) {
    // 线性系统：解析推进可以无缝拼接，任意切分 dt 结果一致
    const SpringParams params = SpringParams::FromFrequency(1.7, 0.35);
    const SpringState start{10.0, -3.0};

    const SpringState single = springAdvance(start, params, 0.37);
    SpringState split = springAdvance(springAdvance(start, params, 0.2), params, 0.17);

    EXPECT_NEAR(split.displacement, single.displacement, 1e-10);
    EXPECT_NEAR(split.velocity, single.velocity, 1e-10);
}

TEST(SpringPhysics, NoTimeNoChange) {
    const SpringParams params = SpringParams::FromFrequency(2.0, 0.5);
    const SpringState start{1.0, 2.0};
    EXPECT_EQ(springAdvance(start, params, 0.0), start);
    EXPECT_EQ(springAdvance(start, params, -1.0), start);
}

TEST(SpringPhysics, SettleTime) {
    const SpringParams params = SpringParams::FromFrequency(2.0, 0.4);
    const SpringState start{100.0, 0.0};

    const double time = springSettleTime(start, params, 1e-3);
    EXPECT_TRUE(std::isfinite(time));
    EXPECT_GT(time, 0.0);
    EXPECT_LT(time, 5.0);

    // 到点之后位移应该落进容差（估计值，留一点余量）
    const double residual = std::abs(springAdvance(start, params, time).displacement);
    EXPECT_LE(residual, 5e-3);

    // 容差放宽，估计时间应变短
    EXPECT_LT(springSettleTime(start, params, 1e-1), time);

    // 已经静止的弹簧时间为 0
    EXPECT_DOUBLE_EQ(springSettleTime(SpringState{0.0, 0.0}, params, 1e-3), 0.0);

    // 无阻尼永不静止
    const SpringParams undamped = makeParams(0.0);
    EXPECT_TRUE(std::isinf(springSettleTime(start, undamped, 1e-3)));
}

TEST(Spring, TypicalChartAnimation) {
    Spring spring(SpringParams::FromFrequency(2.0, 0.7));
    spring.setPosition(0.0);
    spring.setTarget(100.0);
    EXPECT_DOUBLE_EQ(spring.position(), 0.0);
    EXPECT_FALSE(spring.isAtRest());

    double value = 0.0;
    for (int i = 0; i < 60; ++i) {
        value = spring.update(1.0 / 60.0);
    }
    EXPECT_NEAR(value, 100.0, 1.0);
    EXPECT_TRUE(spring.isAtRest(1.0, 20.0));
}

TEST(Spring, TargetSwitchKeepsContinuity) {
    Spring spring(SpringParams::FromFrequency(1.5, 0.8));
    spring.setTarget(10.0);
    for (int i = 0; i < 10; ++i) {
        spring.update(1.0 / 60.0);
    }

    const double positionBefore = spring.position();
    const double velocityBefore = spring.velocity();

    spring.setTarget(-5.0);
    // 打断时位置与速度都连续——这正是弹簧比缓动曲线更像"手感"的原因
    EXPECT_NEAR(spring.position(), positionBefore, 1e-12);
    EXPECT_NEAR(spring.velocity(), velocityBefore, 1e-12);
    EXPECT_DOUBLE_EQ(spring.target(), -5.0);
}

TEST(Spring, SnapAndPositionSetter) {
    Spring spring(SpringParams::FromFrequency(2.0, 0.5));
    spring.setTarget(10.0);
    spring.setVelocity(50.0);
    spring.setPosition(3.0);

    EXPECT_DOUBLE_EQ(spring.position(), 3.0);
    EXPECT_DOUBLE_EQ(spring.velocity(), 50.0);

    spring.snapToTarget();
    EXPECT_DOUBLE_EQ(spring.position(), 10.0);
    EXPECT_DOUBLE_EQ(spring.velocity(), 0.0);
    EXPECT_TRUE(spring.isAtRest());
}

TEST(Spring, ExtremeParamsStayFinite) {
    Spring spring;
    SpringParams broken;
    broken.mass = 0.0;
    broken.stiffness = 0.0;
    broken.damping = -1.0;
    spring.setParams(broken);  // 会被兜底钳制，不应该除零

    spring.setTarget(1000.0);
    double value = 0.0;
    for (int i = 0; i < 100; ++i) {
        value = spring.update(1.0 / 60.0);
    }
    EXPECT_TRUE(std::isfinite(value));

    // 极端刚度 + 超大步长也不发散
    Spring stiff(SpringParams::FromFrequency(60.0, 0.2));
    stiff.setTarget(1e6);
    const double jumped = stiff.update(10.0);
    EXPECT_TRUE(std::isfinite(jumped));
}

}  // namespace
