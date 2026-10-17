#pragma once

// 二维向量：图表里的数据点、像素坐标、法线、方向都走这个类型。
// 头部模板 + constexpr，编译期可算的都在编译期算。

#include <cmath>
#include <concepts>
#include <limits>
#include <type_traits>

#include "MathUtils.h"

namespace kiriyama::math {

template <Number T = float>
struct Vector2D {
    using value_type = T;

    T x{};
    T y{};

    constexpr Vector2D() noexcept = default;
    constexpr Vector2D(T xValue, T yValue) noexcept : x(xValue), y(yValue) {}

    /// 不同精度之间显式转换，避免 float/double 混用被静默吞掉。
    template <Number U>
    [[nodiscard]] constexpr explicit Vector2D(const Vector2D<U>& other) noexcept
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

    // ------------------------------------------------------------ 构造快捷方式

    [[nodiscard]] static constexpr Vector2D Zero() noexcept { return {T{0}, T{0}}; }
    [[nodiscard]] static constexpr Vector2D One() noexcept { return {T{1}, T{1}}; }
    [[nodiscard]] static constexpr Vector2D Splat(T value) noexcept { return {value, value}; }
    [[nodiscard]] static constexpr Vector2D UnitX() noexcept { return {T{1}, T{0}}; }
    [[nodiscard]] static constexpr Vector2D UnitY() noexcept { return {T{0}, T{1}}; }

    /// 极坐标转直角坐标，角度为弧度，0 指向 +X。
    template <std::floating_point U>
    [[nodiscard]] static Vector2D FromAngle(U radians, U length = U{1}) noexcept {
        return {static_cast<T>(std::cos(radians) * length),
                static_cast<T>(std::sin(radians) * length)};
    }

    // ------------------------------------------------------------ 算术运算

    constexpr Vector2D& operator+=(const Vector2D& rhs) noexcept {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    constexpr Vector2D& operator-=(const Vector2D& rhs) noexcept {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    constexpr Vector2D& operator*=(T scalar) noexcept {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    constexpr Vector2D& operator/=(T scalar) noexcept {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    [[nodiscard]] constexpr Vector2D operator+(const Vector2D& rhs) const noexcept {
        return {static_cast<T>(x + rhs.x), static_cast<T>(y + rhs.y)};
    }

    [[nodiscard]] constexpr Vector2D operator-(const Vector2D& rhs) const noexcept {
        return {static_cast<T>(x - rhs.x), static_cast<T>(y - rhs.y)};
    }

    [[nodiscard]] constexpr Vector2D operator-() const noexcept {
        return {static_cast<T>(-x), static_cast<T>(-y)};
    }

    [[nodiscard]] constexpr Vector2D operator*(T scalar) const noexcept {
        return {static_cast<T>(x * scalar), static_cast<T>(y * scalar)};
    }

    [[nodiscard]] constexpr Vector2D operator/(T scalar) const noexcept {
        return {static_cast<T>(x / scalar), static_cast<T>(y / scalar)};
    }

    [[nodiscard]] constexpr bool operator==(const Vector2D& rhs) const noexcept {
        return x == rhs.x && y == rhs.y;
    }

    [[nodiscard]] constexpr bool operator!=(const Vector2D& rhs) const noexcept {
        return !(*this == rhs);
    }

    /// 0 -> x，1 -> y。
    [[nodiscard]] constexpr T operator[](int index) const noexcept {
        return index == 0 ? x : y;
    }

    // ------------------------------------------------------------ 长度

    [[nodiscard]] constexpr T lengthSquared() const noexcept {
        return static_cast<T>(x * x + y * y);
    }

    [[nodiscard]] T length() const noexcept { return std::sqrt(lengthSquared()); }

    /// 曼哈顿长度，做网格吸附 / 粗略距离比较时比开方便宜。
    [[nodiscard]] constexpr T manhattanLength() const noexcept {
        return static_cast<T>(math::abs(x) + math::abs(y));
    }

    [[nodiscard]] constexpr T distanceSquaredTo(const Vector2D& other) const noexcept {
        return (*this - other).lengthSquared();
    }

    [[nodiscard]] T distanceTo(const Vector2D& other) const noexcept {
        return (*this - other).length();
    }

    // ------------------------------------------------------------ 归一化

    /// 零向量返回零向量（不产生 NaN），图表里退化数据很常见，这里不抛错。
    [[nodiscard]] Vector2D normalized() const noexcept requires std::floating_point<T> {
        const T len = length();
        if (len <= std::numeric_limits<T>::epsilon()) {
            return Zero();
        }
        return {static_cast<T>(x / len), static_cast<T>(y / len)};
    }

    /// 保持方向，改长度。零向量返回零向量。
    [[nodiscard]] Vector2D withLength(T newLength) const noexcept requires std::floating_point<T> {
        const T len = length();
        if (len <= std::numeric_limits<T>::epsilon()) {
            return Zero();
        }
        const T factor = static_cast<T>(newLength / len);
        return {static_cast<T>(x * factor), static_cast<T>(y * factor)};
    }

    // ------------------------------------------------------------ 向量运算

    [[nodiscard]] constexpr T dot(const Vector2D& other) const noexcept {
        return static_cast<T>(x * other.x + y * other.y);
    }

    /// 二维叉积的 z 分量：正数表示 other 在本向量逆时针一侧。
    [[nodiscard]] constexpr T cross(const Vector2D& other) const noexcept {
        return static_cast<T>(x * other.y - y * other.x);
    }

    /// 逆时针旋转 90 度。
    [[nodiscard]] constexpr Vector2D perpendicular() const noexcept {
        return {static_cast<T>(-y), x};
    }

    /// 逆时针旋转任意弧度。
    [[nodiscard]] Vector2D rotated(T radians) const noexcept requires std::floating_point<T> {
        const T c = std::cos(radians);
        const T s = std::sin(radians);
        return {static_cast<T>(x * c - y * s), static_cast<T>(x * s + y * c)};
    }

    /// 绕 pivot 旋转。
    [[nodiscard]] Vector2D rotatedAround(const Vector2D& pivot, T radians) const noexcept
        requires std::floating_point<T> {
        return pivot + (*this - pivot).rotated(radians);
    }

    /// 与 +X 轴的夹角，弧度，范围 [-pi, pi]。
    [[nodiscard]] T angle() const noexcept requires std::floating_point<T> {
        return std::atan2(y, x);
    }

    /// 从本向量转到 other 的最短有符号夹角，范围 [-pi, pi)。
    [[nodiscard]] T angleTo(const Vector2D& other) const noexcept
        requires std::floating_point<T> {
        return normalizeAngle(static_cast<T>(other.angle() - angle()));
    }

    /// 投影到 axis 上。axis 为零向量时返回零向量。
    [[nodiscard]] Vector2D projectedOnto(const Vector2D& axis) const noexcept
        requires std::floating_point<T> {
        const T lenSquared = axis.lengthSquared();
        if (lenSquared <= std::numeric_limits<T>::epsilon()) {
            return Zero();
        }
        const T scale = static_cast<T>(dot(axis) / lenSquared);
        return {static_cast<T>(axis.x * scale), static_cast<T>(axis.y * scale)};
    }

    /// 逐分量线性插值。
    [[nodiscard]] constexpr Vector2D lerp(const Vector2D& to, T t) const noexcept {
        return {math::lerp(x, to.x, t), math::lerp(y, to.y, t)};
    }

    // ------------------------------------------------------------ 逐分量工具

    [[nodiscard]] constexpr Vector2D componentMin(const Vector2D& other) const noexcept {
        return {x < other.x ? x : other.x, y < other.y ? y : other.y};
    }

    [[nodiscard]] constexpr Vector2D componentMax(const Vector2D& other) const noexcept {
        return {x > other.x ? x : other.x, y > other.y ? y : other.y};
    }

    [[nodiscard]] constexpr Vector2D abs() const noexcept {
        return {math::abs(x), math::abs(y)};
    }

    /// 分别向下 / 向上取整，用于把坐标对齐到像素栅格。
    [[nodiscard]] Vector2D floored() const noexcept requires std::floating_point<T> {
        return {std::floor(x), std::floor(y)};
    }

    [[nodiscard]] Vector2D ceiled() const noexcept requires std::floating_point<T> {
        return {std::ceil(x), std::ceil(y)};
    }

    [[nodiscard]] Vector2D rounded() const noexcept requires std::floating_point<T> {
        return {std::round(x), std::round(y)};
    }

    // ------------------------------------------------------------ 判定

    [[nodiscard]] constexpr bool isZero(T epsilon = static_cast<T>(kEpsilon)) const noexcept {
        return nearlyZero(x, epsilon) && nearlyZero(y, epsilon);
    }

    [[nodiscard]] bool isFinite() const noexcept requires std::floating_point<T> {
        return std::isfinite(x) && std::isfinite(y);
    }

    [[nodiscard]] constexpr bool nearlyEquals(const Vector2D& other,
                                             T epsilon = static_cast<T>(kEpsilon)) const noexcept {
        return nearlyEqual(x, other.x, epsilon) && nearlyEqual(y, other.y, epsilon);
    }
};

using Vec2 = Vector2D<float>;
using Vec2d = Vector2D<double>;
using Vec2i = Vector2D<int>;

// 标量在左侧的乘法，保证 s * v 和 v * s 两种写法都可用。
template <Number T>
[[nodiscard]] constexpr Vector2D<T> operator*(T scalar, const Vector2D<T>& vector) noexcept {
    return vector * scalar;
}

template <Number T>
[[nodiscard]] constexpr Vector2D<T> lerp(const Vector2D<T>& a, const Vector2D<T>& b, T t) noexcept {
    return a.lerp(b, t);
}

}  // namespace kiriyama::math
