#include "kiriyama/math/Curve.h"

#include <algorithm>
#include <cmath>

namespace kiriyama::math {

std::vector<Vec2d> monotoneCubicSpline(std::span<const Vec2d> points, int segments,
                                       std::size_t maxPoints) {
    if (points.size() < 3 || segments <= 1) {
        return {points.begin(), points.end()};
    }
    const std::size_t n = points.size();
    for (std::size_t i = 1; i < n; ++i) {
        if (!(points[i].x > points[i - 1].x)) {
            return {points.begin(), points.end()};
        }
    }

    const std::size_t spans = n - 1;
    std::size_t perSpan = static_cast<std::size_t>(segments);
    if (maxPoints > 0 && spans * perSpan > maxPoints) {
        perSpan = std::max<std::size_t>(1, maxPoints / spans);
    }
    if (perSpan <= 1) {
        return {points.begin(), points.end()};
    }

    // 1. 计算每段割线斜率 delta[i]
    std::vector<double> delta(spans);
    for (std::size_t i = 0; i < spans; ++i) {
        delta[i] = (points[i + 1].y - points[i].y) / (points[i + 1].x - points[i].x);
    }

    // 2. 初始切线斜率：内部点取相邻割线均值，极值点（割线异号）强制取 0 防止过冲
    std::vector<double> slope(n);
    slope[0] = delta[0];
    slope[n - 1] = delta[spans - 1];
    for (std::size_t i = 1; i + 1 < n; ++i) {
        slope[i] = (delta[i - 1] * delta[i] <= 0.0) ? 0.0 : (delta[i - 1] + delta[i]) * 0.5;
    }

    // 3. Fritsch–Carlson 单调性圆域约束：将 (alpha, beta) 限制在半径为 3 的圆内
    for (std::size_t i = 0; i < spans; ++i) {
        if (delta[i] == 0.0) {
            slope[i] = 0.0;
            slope[i + 1] = 0.0;
            continue;
        }
        const double a = slope[i] / delta[i];
        const double b = slope[i + 1] / delta[i];
        const double sum = a * a + b * b;
        if (sum > 9.0) {
            const double scale = 3.0 / std::sqrt(sum);
            slope[i] = scale * a * delta[i];
            slope[i + 1] = scale * b * delta[i];
        }
    }

    // 4. 按三次 Hermite 基函数采样插值点
    std::vector<Vec2d> out;
    out.reserve(spans * perSpan + 1);
    out.push_back(points.front());
    const double step = 1.0 / static_cast<double>(perSpan);
    for (std::size_t i = 0; i < spans; ++i) {
        const double h = points[i + 1].x - points[i].x;
        const double m0 = slope[i] * h;
        const double m1 = slope[i + 1] * h;
        for (std::size_t k = 1; k <= perSpan; ++k) {
            const double t = step * static_cast<double>(k);
            const double t2 = t * t;
            const double t3 = t2 * t;
            const double y = (2.0 * t3 - 3.0 * t2 + 1.0) * points[i].y +
                             (t3 - 2.0 * t2 + t) * m0 +
                             (-2.0 * t3 + 3.0 * t2) * points[i + 1].y +
                             (t3 - t2) * m1;
            out.push_back({points[i].x + h * t, y});
        }
    }
    return out;
}

}  // namespace kiriyama::math
