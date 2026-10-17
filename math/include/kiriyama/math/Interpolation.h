#pragma once

// 插值工具：在 MathUtils 的 lerp / invLerp 之上，补图表动画常用的几种过渡曲线。
// 全部帧率无关——只要给对 deltaTime，30fps 和 120fps 的观感一致。

#include <cmath>
#include <concepts>

#include "MathUtils.h"
#include "Vector2D.h"

namespace kiriyama::math {

/// 硬切换：x < edge 取 0，否则取 1。
template <std::floating_point T>
[[nodiscard]] constexpr T step(T edge, T x) noexcept {
    return x < edge ? T{0} : T{1};
}

/// Hermite 平滑阶跃，一阶导数在两端为 0：3t² - 2t³。
/// 适合柱状图入场、颜色过渡这类"不要回弹"的动画。
template <std::floating_point T>
[[nodiscard]] constexpr T smoothstep(T edge0, T edge1, T x) noexcept {
    const T t = saturate(invLerp(edge0, edge1, x));
    return t * t * (T{3} - T{2} * t);
}

/// 五次平滑阶跃，二阶导数也连续：6t⁵ - 15t⁴ + 10t³。
/// 比 smoothstep 更"软"，用于相机/视口这类大位移。
template <std::floating_point T>
[[nodiscard]] constexpr T smootherstep(T edge0, T edge1, T x) noexcept {
    const T t = saturate(invLerp(edge0, edge1, x));
    return t * t * t * (t * (t * T{6} - T{15}) + T{10});
}

/// 沿最短弧插值角度，避免 359° -> 1° 时绕远路。
template <std::floating_point T>
[[nodiscard]] inline T lerpAngle(T from, T to, T t) noexcept {
    return from + normalizeAngle(static_cast<T>(to - from)) * t;
}

/// 帧率无关的指数趋近（俗称 lerp smoothing）。
/// smoothing 是衰减速率（1/秒）：越大越快，0 表示完全不动。
/// 等价于"每秒把剩余差距吃掉 (1 - e^-smoothing) 比例"，
/// 因此先推进 dt/2 再推进 dt/2，结果和一步推进 dt 相同。
template <std::floating_point T>
[[nodiscard]] inline T damp(T current, T target, T smoothing, T deltaTime) noexcept {
    if (!(smoothing > T{0}) || !(deltaTime > T{0})) {
        return current;
    }
    return lerp(current, target, T{1} - std::exp(-smoothing * deltaTime));
}

/// damp 的角度版本，走最短弧。
template <std::floating_point T>
[[nodiscard]] inline T dampAngle(T current, T target, T smoothing, T deltaTime) noexcept {
    if (!(smoothing > T{0}) || !(deltaTime > T{0})) {
        return current;
    }
    const T delta = normalizeAngle(static_cast<T>(target - current));
    return current + delta * (T{1} - std::exp(-smoothing * deltaTime));
}

/// 二维版本，用于平移 / 缩放中心这类成对坐标。
template <typename T>
[[nodiscard]] Vector2D<T> damp(const Vector2D<T>& current, const Vector2D<T>& target, T smoothing,
                               T deltaTime) noexcept requires std::floating_point<T> {
    if (!(smoothing > T{0}) || !(deltaTime > T{0})) {
        return current;
    }
    const T k = T{1} - std::exp(-smoothing * deltaTime);
    return {current.x + (target.x - current.x) * k, current.y + (target.y - current.y) * k};
}

}  // namespace kiriyama::math
