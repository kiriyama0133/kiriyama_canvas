#pragma once

// 2D 仿射变换（3x2 矩阵）。
//
//     | m00 m01 m02 |
//     | m10 m11 m12 |
//     |  0   0   1  |
//
// 列向量约定：p' = M · p，所以 a * b 表示"先 b 后 a"。
// 场景树里 world = parent->worldTransform() * local，就是这么用的。
//
// 逆矩阵退化时返回 Identity 而不是 NaN——图表里 0 宽度的节点是常态，
// 一条 NaN 流进命中测试就会污染后面所有的比较。

#include <cmath>

#include "MathUtils.h"
#include "Vector2D.h"

namespace kiriyama::math {

struct Affine2D {
    double m00{1.0};
    double m01{0.0};
    double m02{0.0};
    double m10{0.0};
    double m11{1.0};
    double m12{0.0};

    [[nodiscard]] static constexpr Affine2D Identity() noexcept { return {}; }

    [[nodiscard]] static constexpr Affine2D Translation(Vec2d offset) noexcept {
        return {1.0, 0.0, offset.x, 0.0, 1.0, offset.y};
    }

    [[nodiscard]] static constexpr Affine2D Scaling(Vec2d factor) noexcept {
        return {factor.x, 0.0, 0.0, 0.0, factor.y, 0.0};
    }

    [[nodiscard]] static Affine2D Rotation(double radians) noexcept {
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        return {c, -s, 0.0, s, c, 0.0};
    }

    /// 先缩放，再旋转，最后平移（等价于 Translation * Rotation * Scaling）。
    [[nodiscard]] static Affine2D Trs(Vec2d translation, double radians, Vec2d scale) noexcept {
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        return {c * scale.x, -s * scale.y, translation.x,
                s * scale.x, c * scale.y, translation.y};
    }

    [[nodiscard]] constexpr Vec2d apply(Vec2d point) const noexcept {
        return {m00 * point.x + m01 * point.y + m02, m10 * point.x + m11 * point.y + m12};
    }

    /// 只变换方向，忽略平移。
    [[nodiscard]] constexpr Vec2d applyVector(Vec2d vector) const noexcept {
        return {m00 * vector.x + m01 * vector.y, m10 * vector.x + m11 * vector.y};
    }

    [[nodiscard]] constexpr double determinant() const noexcept {
        return m00 * m11 - m01 * m10;
    }

    [[nodiscard]] bool invertible(double epsilon = kEpsilon) const noexcept {
        const double det = determinant();
        return std::isfinite(det) && std::abs(det) > epsilon;
    }

    /// 不可逆（缩放为 0 / 非有限）时返回 Identity。
    [[nodiscard]] Affine2D inverse(double epsilon = kEpsilon) const noexcept {
        const double det = determinant();
        if (!std::isfinite(det) || std::abs(det) <= epsilon) {
            return Identity();
        }
        const double inv = 1.0 / det;
        const double i00 = m11 * inv;
        const double i01 = -m01 * inv;
        const double i10 = -m10 * inv;
        const double i11 = m00 * inv;
        return {i00, i01, -(i00 * m02 + i01 * m12), i10, i11, -(i10 * m02 + i11 * m12)};
    }

    [[nodiscard]] constexpr Affine2D operator*(const Affine2D& rhs) const noexcept {
        return {m00 * rhs.m00 + m01 * rhs.m10,
                m00 * rhs.m01 + m01 * rhs.m11,
                m00 * rhs.m02 + m01 * rhs.m12 + m02,
                m10 * rhs.m00 + m11 * rhs.m10,
                m10 * rhs.m01 + m11 * rhs.m11,
                m10 * rhs.m02 + m11 * rhs.m12 + m12};
    }

    constexpr Affine2D& operator*=(const Affine2D& rhs) noexcept {
        *this = *this * rhs;
        return *this;
    }

    [[nodiscard]] constexpr bool isIdentity(double epsilon = kEpsilon) const noexcept {
        return nearlyZero(m00 - 1.0, epsilon) && nearlyZero(m01, epsilon) &&
               nearlyZero(m02, epsilon) && nearlyZero(m10, epsilon) &&
               nearlyZero(m11 - 1.0, epsilon) && nearlyZero(m12, epsilon);
    }

    [[nodiscard]] constexpr bool operator==(const Affine2D&) const noexcept = default;
};

}  // namespace kiriyama::math
