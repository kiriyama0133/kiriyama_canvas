#pragma once

// 标量数学工具：常量、插值、比较、角度、取整。
// 全部为 constexpr / inline，无全局状态，可安全用于 wasm。

#include <cmath>
#include <concepts>
#include <type_traits>

namespace kiriyama::math {

/// 允许参与算术的标量类型（含整型），几何运算另由 std::floating_point 约束。
template <typename T>
concept Number = std::is_arithmetic_v<T>;

// ---------------------------------------------------------------- 常量

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;
inline constexpr double kHalfPi = 0.5 * kPi;
inline constexpr double kDegToRad = kPi / 180.0;
inline constexpr double kRadToDeg = 180.0 / kPi;

/// 默认比较容差（绝对误差）。图表坐标量级一般在 1e4 以内，1e-9 足够。
inline constexpr double kEpsilon = 1e-9;

// ---------------------------------------------------------------- 限幅 / 插值

/// 将 value 限制在 [low, high]，要求 low <= high。
template <Number T>
[[nodiscard]] constexpr T clamp(T value, T low, T high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

/// 将 value 限制在 [0, 1]。
template <Number T>
[[nodiscard]] constexpr T saturate(T value) noexcept {
    return clamp(value, T{0}, T{1});
}

/// 线性插值：t = 0 返回 a，t = 1 返回 b，t 超出 [0, 1] 时为外插。
template <Number T>
[[nodiscard]] constexpr T lerp(T a, T b, T t) noexcept {
    return a + (b - a) * t;
}

/// 线性插值，t 先被夹到 [0, 1]。
template <Number T>
[[nodiscard]] constexpr T lerpClamped(T a, T b, T t) noexcept {
    return lerp(a, b, saturate(t));
}

/// lerp 的反函数：返回 value 在 [a, b] 中的归一化位置。
/// a == b 时返回 0，避免 NaN 扩散到布局计算里。
template <Number T>
[[nodiscard]] constexpr T invLerp(T a, T b, T value) noexcept {
    return a == b ? T{0} : static_cast<T>((value - a) / (b - a));
}

/// 把 value 从 [inLow, inHigh] 映射到 [outLow, outHigh]。
template <Number T>
[[nodiscard]] constexpr T remap(T value, T inLow, T inHigh, T outLow, T outHigh) noexcept {
    return lerp(outLow, outHigh, invLerp(inLow, inHigh, value));
}

/// remap，但结果被夹在输出区间内。
template <Number T>
[[nodiscard]] constexpr T remapClamped(T value, T inLow, T inHigh, T outLow, T outHigh) noexcept {
    return lerp(outLow, outHigh, saturate(invLerp(inLow, inHigh, value)));
}

// ---------------------------------------------------------------- 比较

/// 返回 -1 / 0 / 1。
template <Number T>
[[nodiscard]] constexpr int sign(T value) noexcept {
    return (T{0} < value) - (value < T{0});
}

template <Number T>
[[nodiscard]] constexpr T abs(T value) noexcept {
    return value < T{0} ? static_cast<T>(-value) : value;
}

/// 绝对值比较，用于判断"近似为零"。
template <Number T>
[[nodiscard]] constexpr bool nearlyZero(T value, T epsilon = static_cast<T>(kEpsilon)) noexcept {
    return abs(value) <= epsilon;
}

/// 绝对误差比较。
template <Number T>
[[nodiscard]] constexpr bool nearlyEqual(T a, T b, T epsilon = static_cast<T>(kEpsilon)) noexcept {
    return nearlyZero(a - b, epsilon);
}

/// 相对误差比较：适合跨越多个数量级的数据（例如对数轴上的值）。
template <Number T>
[[nodiscard]] constexpr bool nearlyEqualRelative(T a, T b,
                                                T epsilon = static_cast<T>(kEpsilon)) noexcept {
    const T diff = abs(a - b);
    const T scale = static_cast<T>(1) > abs(a) ? static_cast<T>(1) : abs(a);
    const T limit = abs(b) > scale ? abs(b) : scale;
    return diff <= epsilon * limit;
}

// ---------------------------------------------------------------- 角度

template <std::floating_point T>
[[nodiscard]] constexpr T degToRad(T degrees) noexcept {
    return degrees * static_cast<T>(kDegToRad);
}

template <std::floating_point T>
[[nodiscard]] constexpr T radToDeg(T radians) noexcept {
    return radians * static_cast<T>(kRadToDeg);
}

/// 把角度折回 [-pi, pi)，图表里常用于把向量夹角变成有符号的最小角。
template <std::floating_point T>
[[nodiscard]] inline T normalizeAngle(T radians) noexcept {
    const T twoPi = static_cast<T>(kTwoPi);
    const T pi = static_cast<T>(kPi);
    T wrapped = std::fmod(radians + pi, twoPi);
    if (wrapped < T{0}) {
        wrapped += twoPi;
    }
    return wrapped - pi;
}

// ---------------------------------------------------------------- 其它

template <Number T>
[[nodiscard]] constexpr T square(T value) noexcept {
    return value * value;
}

/// 除零保护：分母为 0 时返回 fallback。
template <Number T>
[[nodiscard]] constexpr T safeDiv(T numerator, T denominator, T fallback = T{0}) noexcept {
    return denominator == T{0} ? fallback : static_cast<T>(numerator / denominator);
}

/// 小数部分，结果落在 [0, 1)。
template <std::floating_point T>
[[nodiscard]] inline T fract(T value) noexcept {
    return value - std::floor(value);
}

/// 把 value 吸附到 step 的整数倍（四舍五入）。step <= 0 时原样返回。
template <std::floating_point T>
[[nodiscard]] inline T snapTo(T value, T step) noexcept {
    if (!(step > T{0})) {
        return value;
    }
    return std::round(value / step) * step;
}

/// 把 value 向上吸附到 step 的整数倍，step <= 0 时原样返回。
template <std::floating_point T>
[[nodiscard]] inline T snapUpTo(T value, T step) noexcept {
    if (!(step > T{0})) {
        return value;
    }
    return std::ceil(value / step) * step;
}

/// 把 value 向下吸附到 step 的整数倍，step <= 0 时原样返回。
template <std::floating_point T>
[[nodiscard]] inline T snapDownTo(T value, T step) noexcept {
    if (!(step > T{0})) {
        return value;
    }
    return std::floor(value / step) * step;
}

}  // namespace kiriyama::math
