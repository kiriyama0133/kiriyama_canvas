#include <gtest/gtest.h>

#include <cmath>

#include "kiriyama/math/Interpolation.h"

namespace {

using namespace kiriyama::math;

TEST(Interpolation, Step) {
    EXPECT_DOUBLE_EQ(step(0.5, 0.4), 0.0);
    EXPECT_DOUBLE_EQ(step(0.5, 0.5), 1.0);
    EXPECT_DOUBLE_EQ(step(0.5, 0.6), 1.0);
}

TEST(Interpolation, Smoothstep) {
    EXPECT_DOUBLE_EQ(smoothstep(0.0, 1.0, -1.0), 0.0);
    EXPECT_DOUBLE_EQ(smoothstep(0.0, 1.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(smoothstep(0.0, 1.0, 0.5), 0.5);
    EXPECT_DOUBLE_EQ(smoothstep(0.0, 1.0, 1.0), 1.0);
    EXPECT_DOUBLE_EQ(smoothstep(0.0, 1.0, 2.0), 1.0);

    // 两端导数趋于 0：曲线在接近端点时变平
    EXPECT_LT(smoothstep(0.0, 1.0, 0.01), 0.01);
    EXPECT_GT(smoothstep(0.0, 1.0, 0.99), 0.99);

    // 区间退化时不应产生 NaN
    const double degenerate = smoothstep(1.0, 1.0, 1.0);
    EXPECT_TRUE(std::isfinite(degenerate));

    // 单调递增
    double previous = -1.0;
    for (int i = 0; i <= 20; ++i) {
        const double value = smoothstep(10.0, 20.0, 10.0 + static_cast<double>(i) * 0.5);
        EXPECT_GE(value, previous);
        previous = value;
    }
}

TEST(Interpolation, Smootherstep) {
    EXPECT_DOUBLE_EQ(smootherstep(0.0, 1.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(smootherstep(0.0, 1.0, 0.5), 0.5);
    EXPECT_DOUBLE_EQ(smootherstep(0.0, 1.0, 1.0), 1.0);
    // 比 smoothstep 更"贴边"
    EXPECT_LT(smootherstep(0.0, 1.0, 0.1), smoothstep(0.0, 1.0, 0.1));
}

TEST(Interpolation, LerpAngle) {
    const double a = degToRad(350.0);
    const double b = degToRad(10.0);
    // 走最短弧：中点应是 360°/0°，而不是绕远路到 180°
    const double mid = lerpAngle(a, b, 0.5);
    EXPECT_NEAR(std::cos(mid), 1.0, 1e-9);
    EXPECT_NEAR(std::sin(mid), 0.0, 1e-9);

    EXPECT_NEAR(lerpAngle(0.0, kHalfPi, 0.5), kPi / 4.0, 1e-12);
}

TEST(Interpolation, Damp) {
    // 帧率无关：两步 dt/2 == 一步 dt
    const double single = damp(0.0, 100.0, 8.0, 1.0 / 30.0);
    const double half = damp(damp(0.0, 100.0, 8.0, 1.0 / 60.0), 100.0, 8.0, 1.0 / 60.0);
    EXPECT_NEAR(single, half, 1e-12);

    // 单调逼近目标，永远不会冲过
    double value = 0.0;
    for (int i = 0; i < 200; ++i) {
        value = damp(value, 100.0, 8.0, 1.0 / 60.0);
        EXPECT_LE(value, 100.0);
    }
    EXPECT_NEAR(value, 100.0, 1.0);

    // dt 或速率非正时原样返回
    EXPECT_DOUBLE_EQ(damp(3.0, 100.0, 8.0, 0.0), 3.0);
    EXPECT_DOUBLE_EQ(damp(3.0, 100.0, 0.0, 1.0), 3.0);
}

TEST(Interpolation, DampAngle) {
    // 从 350° 逼近 10°，应跨过 0°，而不是反向走 340°
    const double from = degToRad(350.0);
    const double to = degToRad(10.0);
    const double value = dampAngle(from, to, 10.0, 0.1);
    // 结果应该落在 350° 与 370° (=10°) 之间
    const double degrees = radToDeg(value);
    EXPECT_GT(degrees, 350.0);
    EXPECT_LT(degrees, 370.0);
}

TEST(Interpolation, DampVector) {
    const Vec2 from{0.0f, 0.0f};
    const Vec2 to{100.0f, -50.0f};
    const Vec2 value = damp(from, to, 10.0f, 0.1f);
    EXPECT_GT(value.x, 0.0f);
    EXPECT_LT(value.x, 100.0f);
    EXPECT_LT(value.y, 0.0f);
    EXPECT_GT(value.y, -50.0f);

    // 两步半程 == 一步全程
    const Vec2 half = damp(damp(from, to, 10.0f, 0.05f), to, 10.0f, 0.05f);
    const Vec2 single = damp(from, to, 10.0f, 0.1f);
    EXPECT_NEAR(half.x, single.x, 1e-5f);
    EXPECT_NEAR(half.y, single.y, 1e-5f);
}

}  // namespace
