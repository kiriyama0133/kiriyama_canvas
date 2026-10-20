#pragma once

// 曲线插值数学工具（math 层，零外部依赖）：
//   · Fritsch–Carlson 单调三次 Hermite 样条插值（monotoneCubicSpline）：
//     保证在单调数据区间内绝不过冲（不会在陡增段鼓出假峰值），极值点处自动将切线斜率置 0；
//     若输入点序列 x 不严格递增，安全退回原折线顶点。

#include <cstddef>
#include <span>
#include <vector>

#include "Vector2D.h"

namespace kiriyama::math {

/// 曲线细分的总点数默认上限，防止超大数据系列 × 高细分数撑爆内存。
inline constexpr std::size_t kDefaultMaxCurvePoints = 4000;

/// 单调三次 Hermite 插值（Fritsch–Carlson 算法）。
/// @param points     输入折线顶点序列（要求按 x 严格递增，否则直接返回原序列拷贝）
/// @param segments   相邻两数据点之间的插值细分数（<= 1 时退回原折线）
/// @param maxPoints  输出总顶点数上限
[[nodiscard]] std::vector<Vec2d> monotoneCubicSpline(
    std::span<const Vec2d> points, int segments,
    std::size_t maxPoints = kDefaultMaxCurvePoints);

}  // namespace kiriyama::math
