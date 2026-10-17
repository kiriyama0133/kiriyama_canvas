#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "kiriyama/math/Math.h"

namespace {

using namespace kiriyama::math;

// ---------------------------------------------------------------- MathUtils

TEST(MathUtils, ClampAndSaturate) {
    EXPECT_FLOAT_EQ(clamp(5.0f, 0.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(clamp(-5.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(clamp(0.4f, 0.0f, 1.0f), 0.4f);
    EXPECT_DOUBLE_EQ(saturate(1.7), 1.0);
    EXPECT_DOUBLE_EQ(saturate(-0.2), 0.0);
    EXPECT_EQ(clamp(7, 0, 3), 3);
}

TEST(MathUtils, LerpFamily) {
    EXPECT_DOUBLE_EQ(lerp(10.0, 20.0, 0.0), 10.0);
    EXPECT_DOUBLE_EQ(lerp(10.0, 20.0, 1.0), 20.0);
    EXPECT_DOUBLE_EQ(lerp(10.0, 20.0, 0.25), 12.5);
    // 超出 [0,1] 时继续外插
    EXPECT_DOUBLE_EQ(lerp(10.0, 20.0, 2.0), 30.0);
    EXPECT_DOUBLE_EQ(lerpClamped(10.0, 20.0, 2.0), 20.0);

    EXPECT_DOUBLE_EQ(invLerp(10.0, 20.0, 15.0), 0.5);
    // 区间退化时返回 0，不产生 NaN
    EXPECT_DOUBLE_EQ(invLerp(5.0, 5.0, 5.0), 0.0);

    EXPECT_DOUBLE_EQ(remap(5.0, 0.0, 10.0, 100.0, 200.0), 150.0);
    EXPECT_DOUBLE_EQ(remapClamped(20.0, 0.0, 10.0, 100.0, 200.0), 200.0);
}

TEST(MathUtils, Comparison) {
    EXPECT_EQ(sign(-3.0), -1);
    EXPECT_EQ(sign(0.0), 0);
    EXPECT_EQ(sign(2.5), 1);

    EXPECT_TRUE(nearlyZero(1e-12));
    EXPECT_FALSE(nearlyZero(1e-6));
    EXPECT_TRUE(nearlyEqual(0.1 + 0.2, 0.3, 1e-9));
    EXPECT_FALSE(nearlyEqual(0.1 + 0.2, 0.3, 1e-18));

    // 相对比较：1e9 量级上允许更大的绝对误差
    EXPECT_TRUE(nearlyEqualRelative(1e9, 1e9 + 1.0, 1e-6));
    EXPECT_FALSE(nearlyEqualRelative(1.0, 1.1, 1e-6));

    EXPECT_DOUBLE_EQ(safeDiv(1.0, 0.0, -1.0), -1.0);
    EXPECT_DOUBLE_EQ(safeDiv(9.0, 3.0), 3.0);
    EXPECT_DOUBLE_EQ(square(3.0), 9.0);
}

TEST(MathUtils, Angles) {
    EXPECT_NEAR(degToRad(180.0), kPi, 1e-12);
    EXPECT_NEAR(radToDeg(kHalfPi), 90.0, 1e-12);
    EXPECT_NEAR(degToRad(90.0f), 1.5707964f, 1e-6f);

    // 折回 [-pi, pi)，因此 3pi 落在 -pi 而不是 pi
    EXPECT_NEAR(normalizeAngle(3.0 * kPi), -kPi, 1e-12);
    EXPECT_NEAR(normalizeAngle(-3.0 * kPi), -kPi, 1e-12);
    EXPECT_NEAR(normalizeAngle(0.25), 0.25, 1e-12);
}

TEST(MathUtils, SnappingAndFractions) {
    EXPECT_NEAR(fract(3.25), 0.25, 1e-12);
    EXPECT_NEAR(fract(-0.25), 0.75, 1e-12);

    EXPECT_DOUBLE_EQ(snapTo(12.4, 5.0), 10.0);
    EXPECT_DOUBLE_EQ(snapTo(12.6, 5.0), 15.0);
    EXPECT_DOUBLE_EQ(snapUpTo(12.1, 5.0), 15.0);
    EXPECT_DOUBLE_EQ(snapDownTo(12.9, 5.0), 10.0);
    // step <= 0 时原样返回
    EXPECT_DOUBLE_EQ(snapTo(12.4, 0.0), 12.4);
}

// ---------------------------------------------------------------- Vector2D

TEST(Vector2D, Construction) {
    constexpr Vec2 v{3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v.x, 3.0f);
    EXPECT_FLOAT_EQ(v.y, 4.0f);
    EXPECT_FLOAT_EQ(v[0], 3.0f);
    EXPECT_FLOAT_EQ(v[1], 4.0f);

    EXPECT_EQ(Vec2::Zero(), (Vec2{0.0f, 0.0f}));
    EXPECT_EQ(Vec2::Splat(2.0f), (Vec2{2.0f, 2.0f}));
    EXPECT_EQ(Vec2::UnitY(), (Vec2{0.0f, 1.0f}));

    // 精度转换必须显式
    const Vec2d wide{v};
    EXPECT_DOUBLE_EQ(wide.x, 3.0);

    const Vec2i pixel{4, 5};
    EXPECT_EQ(pixel.x, 4);
    EXPECT_EQ(pixel.lengthSquared(), 16 + 25);
}

TEST(Vector2D, Arithmetic) {
    const Vec2 a{1.0f, 2.0f};
    const Vec2 b{3.0f, 5.0f};

    EXPECT_EQ(a + b, (Vec2{4.0f, 7.0f}));
    EXPECT_EQ(b - a, (Vec2{2.0f, 3.0f}));
    EXPECT_EQ(-a, (Vec2{-1.0f, -2.0f}));
    EXPECT_EQ(a * 2.0f, (Vec2{2.0f, 4.0f}));
    EXPECT_EQ(2.0f * a, (Vec2{2.0f, 4.0f}));
    EXPECT_EQ(a / 2.0f, (Vec2{0.5f, 1.0f}));

    Vec2 acc{1.0f, 2.0f};
    acc += b;
    acc *= 2.0f;
    acc -= a;
    acc /= 2.0f;
    EXPECT_TRUE(acc.nearlyEquals(Vec2{3.5f, 6.0f}));
}

TEST(Vector2D, Length) {
    const Vec2 v{3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v.lengthSquared(), 25.0f);
    EXPECT_FLOAT_EQ(v.length(), 5.0f);
    EXPECT_FLOAT_EQ(v.manhattanLength(), 7.0f);
    EXPECT_FLOAT_EQ(v.distanceTo(Vec2{0.0f, 0.0f}), 5.0f);
    EXPECT_FLOAT_EQ(v.distanceSquaredTo(Vec2{3.0f, 0.0f}), 16.0f);
}

TEST(Vector2D, Normalization) {
    const Vec2 v{0.0f, -7.0f};
    const Vec2 n = v.normalized();
    EXPECT_FLOAT_EQ(n.x, 0.0f);
    EXPECT_FLOAT_EQ(n.y, -1.0f);
    EXPECT_FLOAT_EQ(v.withLength(2.0f).length(), 2.0f);

    // 零向量不产生 NaN
    const Vec2 zero = Vec2::Zero().normalized();
    EXPECT_TRUE(zero.isZero());
    EXPECT_TRUE(Vec2::Zero().withLength(5.0f).isZero());
}

TEST(Vector2D, DotCrossPerpendicular) {
    const Vec2 a{1.0f, 0.0f};
    const Vec2 b{0.0f, 1.0f};
    EXPECT_FLOAT_EQ(a.dot(b), 0.0f);
    EXPECT_FLOAT_EQ(a.dot(a), 1.0f);
    EXPECT_FLOAT_EQ(a.cross(b), 1.0f);
    EXPECT_FLOAT_EQ(b.cross(a), -1.0f);

    // 逆时针 90 度
    EXPECT_EQ(a.perpendicular(), b);
}

TEST(Vector2D, Rotation) {
    const Vec2 v{1.0f, 0.0f};
    const Vec2 rotated = v.rotated(static_cast<float>(kHalfPi));
    EXPECT_NEAR(rotated.x, 0.0f, 1e-6f);
    EXPECT_NEAR(rotated.y, 1.0f, 1e-6f);

    const Vec2 spun = Vec2{2.0f, 0.0f}.rotatedAround(Vec2{1.0f, 0.0f},
                                                     static_cast<float>(kPi));
    EXPECT_NEAR(spun.x, 0.0f, 1e-6f);
    EXPECT_NEAR(spun.y, 0.0f, 1e-6f);

    EXPECT_NEAR((Vec2{1.0f, 0.0f}).angle(), 0.0f, 1e-6f);
    EXPECT_NEAR((Vec2{0.0f, 1.0f}).angle(), static_cast<float>(kHalfPi), 1e-6f);
    // 最短有符号夹角
    EXPECT_NEAR((Vec2{1.0f, 0.0f}).angleTo(Vec2{0.0f, -1.0f}),
                -static_cast<float>(kHalfPi), 1e-6f);
}

TEST(Vector2D, ProjectionAndInterpolation) {
    const Vec2 v{2.0f, 3.0f};
    const Vec2 projected = v.projectedOnto(Vec2{1.0f, 0.0f});
    EXPECT_NEAR(projected.x, 2.0f, 1e-6f);
    EXPECT_NEAR(projected.y, 0.0f, 1e-6f);
    EXPECT_TRUE(v.projectedOnto(Vec2::Zero()).isZero());

    const Vec2 mid = lerp(Vec2{0.0f, 0.0f}, Vec2{10.0f, 20.0f}, 0.5f);
    EXPECT_FLOAT_EQ(mid.x, 5.0f);
    EXPECT_FLOAT_EQ(mid.y, 10.0f);
}

TEST(Vector2D, Componentwise) {
    const Vec2 a{1.0f, 5.0f};
    const Vec2 b{3.0f, 2.0f};
    EXPECT_EQ(a.componentMin(b), (Vec2{1.0f, 2.0f}));
    EXPECT_EQ(a.componentMax(b), (Vec2{3.0f, 5.0f}));
    EXPECT_EQ((Vec2{-1.5f, 2.5f}).abs(), (Vec2{1.5f, 2.5f}));
    EXPECT_EQ((Vec2{1.4f, 2.6f}).floored(), (Vec2{1.0f, 2.0f}));
    EXPECT_EQ((Vec2{1.4f, 2.6f}).ceiled(), (Vec2{2.0f, 3.0f}));
    EXPECT_EQ((Vec2{1.4f, 2.6f}).rounded(), (Vec2{1.0f, 3.0f}));
}

TEST(Vector2D, Predicates) {
    EXPECT_TRUE((Vec2{1e-12f, -1e-12f}).isZero());
    EXPECT_FALSE((Vec2{1e-3f, 0.0f}).isZero());
    EXPECT_TRUE((Vec2{1.0f, 2.0f}).isFinite());
    EXPECT_FALSE((Vec2{std::nanf(""), 0.0f}).isFinite());
    EXPECT_TRUE((Vec2{1.0f, 2.0f}).nearlyEquals(Vec2{1.0f, 2.0f + 1e-8f}));
}

// ---------------------------------------------------------------- Range

TEST(Range, Basics) {
    const RangeD range{0.0, 100.0};
    EXPECT_DOUBLE_EQ(range.span(), 100.0);
    EXPECT_DOUBLE_EQ(range.midpoint(), 50.0);
    EXPECT_FALSE(range.empty());
    EXPECT_FALSE(range.degenerate());

    EXPECT_TRUE(range.contains(0.0));
    EXPECT_TRUE(range.contains(100.0));
    EXPECT_FALSE(range.contains(100.1));
    EXPECT_TRUE(range.contains(RangeD{10.0, 20.0}));
    EXPECT_TRUE(range.overlaps(RangeD{90.0, 110.0}));
    EXPECT_FALSE(range.overlaps(RangeD{200.0, 210.0}));

    // 自动排序
    EXPECT_EQ(RangeD::FromValues(100.0, 0.0), range);
    // maximum < minimum 视为空
    EXPECT_TRUE((RangeD{10.0, 5.0}).empty());
    EXPECT_FALSE((RangeD{10.0, 5.0}).contains(7.0));
}

TEST(Range, Transforms) {
    RangeD range{0.0, 100.0};
    EXPECT_DOUBLE_EQ(range.normalize(25.0), 0.25);
    EXPECT_DOUBLE_EQ(range.valueAt(0.25), 25.0);
    EXPECT_DOUBLE_EQ(range.clamp(150.0), 100.0);

    range.expandToInclude(-20.0);
    EXPECT_DOUBLE_EQ(range.minimum(), -20.0);
    range.expandToInclude(RangeD{50.0, 300.0});
    EXPECT_DOUBLE_EQ(range.maximum(), 300.0);

    EXPECT_EQ((RangeD{0.0, 10.0}).expanded(2.0), (RangeD{-2.0, 12.0}));
    EXPECT_EQ((RangeD{0.0, 10.0}).unitedWith(RangeD{5.0, 20.0}), (RangeD{0.0, 20.0}));
    EXPECT_EQ((RangeD{0.0, 10.0}).intersectedWith(RangeD{5.0, 20.0}), (RangeD{5.0, 10.0}));

    // 退化区间做归一化不产生 NaN
    EXPECT_DOUBLE_EQ((RangeD{7.0, 7.0}).normalize(7.0), 0.0);
}

// ---------------------------------------------------------------- Scale

TEST(LinearScale, Mapping) {
    // 典型 y 轴：数据 [0, 100] 映射到像素 [400, 0]（屏幕向下）
    const LinearScale scale{RangeD{0.0, 100.0}, RangeD{400.0, 0.0}};
    EXPECT_TRUE(scale.valid());
    EXPECT_DOUBLE_EQ(scale.map(0.0), 400.0);
    EXPECT_DOUBLE_EQ(scale.map(100.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.map(25.0), 300.0);
    EXPECT_DOUBLE_EQ(scale.scaleFactor(), -4.0);

    EXPECT_DOUBLE_EQ(scale.invert(400.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.invert(0.0), 100.0);
    EXPECT_DOUBLE_EQ(scale.invert(300.0), 25.0);

    EXPECT_DOUBLE_EQ(scale.normalize(25.0), 0.25);
    EXPECT_DOUBLE_EQ(scale.positionAt(0.25), 300.0);

    // 夹取后不越界
    EXPECT_DOUBLE_EQ(scale.mapClamped(150.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.mapClamped(-50.0), 400.0);
}

TEST(LinearScale, Degenerate) {
    const LinearScale flat{RangeD{5.0, 5.0}, RangeD{0.0, 100.0}};
    EXPECT_FALSE(flat.valid());
    EXPECT_DOUBLE_EQ(flat.map(5.0), 0.0);
    EXPECT_DOUBLE_EQ(flat.invert(50.0), 5.0);
    EXPECT_DOUBLE_EQ(flat.scaleFactor(), 0.0);
}

TEST(LogScale, Mapping) {
    const LogScale scale{RangeD{1.0, 1000.0}, RangeD{0.0, 300.0}};
    EXPECT_TRUE(scale.valid());
    EXPECT_DOUBLE_EQ(scale.map(1.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.map(10.0), 100.0);
    EXPECT_DOUBLE_EQ(scale.map(100.0), 200.0);
    EXPECT_DOUBLE_EQ(scale.map(1000.0), 300.0);

    EXPECT_NEAR(scale.invert(100.0), 10.0, 1e-9);
    EXPECT_NEAR(scale.invert(scale.map(37.0)), 37.0, 1e-9);

    // 非正值被夹到下界，不产生 NaN / inf
    EXPECT_DOUBLE_EQ(scale.map(0.0), 0.0);
    EXPECT_DOUBLE_EQ(scale.map(-5.0), 0.0);

    const LogScale invalid{RangeD{-10.0, -1.0}, RangeD{0.0, 100.0}};
    EXPECT_GT(invalid.domain().minimum(), 0.0);
}

// ---------------------------------------------------------------- Ticks

TEST(Ticks, NiceNumber) {
    EXPECT_DOUBLE_EQ(niceNumber(97.0), 100.0);
    EXPECT_DOUBLE_EQ(niceNumber(52.0), 50.0);
    EXPECT_DOUBLE_EQ(niceNumber(0.0234), 0.02);
    EXPECT_DOUBLE_EQ(niceNumber(1.0), 1.0);
    // 向上取整模式不会小于输入
    EXPECT_DOUBLE_EQ(niceNumber(11.0, false), 20.0);
    EXPECT_DOUBLE_EQ(niceNumber(1.1, false), 2.0);
    // 非法输入兜底
    EXPECT_DOUBLE_EQ(niceNumber(0.0), 1.0);
    EXPECT_DOUBLE_EQ(niceNumber(-5.0), 1.0);

    EXPECT_DOUBLE_EQ(niceStep(100.0, 5), 20.0);
    EXPECT_DOUBLE_EQ(niceStep(100.0, 4), 50.0);
}

TEST(Ticks, Linear) {
    const std::vector<double> ticks = linearTicks(RangeD{0.0, 100.0}, 5);
    ASSERT_EQ(ticks.size(), 6u);
    for (int i = 0; i < 6; ++i) {
        EXPECT_NEAR(ticks[static_cast<std::size_t>(i)], static_cast<double>(i) * 20.0, 1e-9);
    }

    // 首尾必须包住 domain
    const std::vector<double> odd = linearTicks(RangeD{3.0, 47.0}, 5);
    ASSERT_FALSE(odd.empty());
    EXPECT_LE(odd.front(), 3.0);
    EXPECT_GE(odd.back(), 47.0);

    // 退化区间
    const std::vector<double> single = linearTicks(RangeD{7.0, 7.0}, 5);
    ASSERT_EQ(single.size(), 1u);
    EXPECT_DOUBLE_EQ(single[0], 7.0);

    // 空区间
    EXPECT_TRUE(linearTicks(RangeD{10.0, 5.0}, 5).empty());

    // 负区间 + 浮点残渣
    const std::vector<double> negative = linearTicks(RangeD{-35.0, 12.0}, 5);
    ASSERT_FALSE(negative.empty());
    EXPECT_LE(negative.front(), -35.0);
    EXPECT_GE(negative.back(), 12.0);
    for (std::size_t i = 1; i < negative.size(); ++i) {
        EXPECT_GT(negative[i], negative[i - 1]);
    }
}

TEST(Ticks, NiceDomain) {
    EXPECT_EQ(niceDomain(RangeD{3.0, 47.0}, 5), (RangeD{0.0, 50.0}));
    // 强制含 0
    EXPECT_EQ(niceDomain(RangeD{12.0, 47.0}, 5, true), (RangeD{0.0, 50.0}));
    // 单点数据也要有可视范围
    const RangeD single = niceDomain(RangeD{10.0, 10.0}, 5);
    EXPECT_LT(single.minimum(), 10.0);
    EXPECT_GT(single.maximum(), 10.0);
    // 空区间原样返回
    EXPECT_TRUE(niceDomain(RangeD{10.0, 5.0}, 5).empty());
}

TEST(Ticks, Log) {
    const std::vector<double> ticks = logTicks(RangeD{1.0, 1000.0});
    ASSERT_EQ(ticks.size(), 4u);
    EXPECT_DOUBLE_EQ(ticks[0], 1.0);
    EXPECT_DOUBLE_EQ(ticks[1], 10.0);
    EXPECT_DOUBLE_EQ(ticks[2], 100.0);
    EXPECT_DOUBLE_EQ(ticks[3], 1000.0);

    // 只取落在区间内的 10 的幂
    EXPECT_EQ(logTicks(RangeD{5.0, 500.0}), (std::vector<double>{10.0, 100.0}));
    // 非正区间无刻度
    EXPECT_TRUE(logTicks(RangeD{-1.0, 0.0}).empty());
}

}  // namespace
