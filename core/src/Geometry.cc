#include "kiriyama/core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kiriyama::core {

std::vector<bool> resolveLabelOverlaps(std::span<const RectD> boxes, double gap) {
    std::vector<bool> visible(boxes.size(), true);
    if (boxes.empty()) {
        return visible;
    }

    const double spacing = std::max(0.0, gap);
    std::vector<std::size_t> kept;
    kept.reserve(boxes.size());

    for (std::size_t i = 0; i < boxes.size(); ++i) {
        // 空标签（空字符串）不占位置，直接保持可见。
        if (boxes[i].empty()) {
            continue;
        }

        const RectD inflated = boxes[i].expanded(spacing);
        bool collides = false;
        for (const std::size_t keptIndex : kept) {
            if (inflated.intersects(boxes[keptIndex])) {
                collides = true;
                break;
            }
        }

        visible[i] = !collides;
        if (!collides) {
            kept.push_back(i);
        }
    }
    return visible;
}

// ---------------------------------------------------------------- 多边形 / 变换

RectD transformedBounds(const math::Affine2D& transform, const RectD& rect) noexcept {
    // 不能因为 rect.empty() 就返回 {}：水平线 / 细条的包围盒高度为 0，
    // 但它的位置仍然是有效信息（命中测试要靠它做剪枝）。
    const Vec2d corners[4] = {
        transform.apply({rect.left(), rect.top()}),
        transform.apply({rect.right(), rect.top()}),
        transform.apply({rect.right(), rect.bottom()}),
        transform.apply({rect.left(), rect.bottom()}),
    };
    double left = corners[0].x;
    double right = left;
    double top = corners[0].y;
    double bottom = top;
    for (const Vec2d& corner : corners) {
        left = std::fmin(left, corner.x);
        right = std::fmax(right, corner.x);
        top = std::fmin(top, corner.y);
        bottom = std::fmax(bottom, corner.y);
    }
    return RectD::FromLTRB(left, top, right, bottom);
}

bool polygonContains(std::span<const Vec2d> points, const Vec2d& point) noexcept {
    const std::size_t count = points.size();
    if (count < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
        const Vec2d& a = points[i];
        const Vec2d& b = points[j];
        const bool straddles = ((a.y > point.y) != (b.y > point.y));
        if (!straddles) {
            continue;
        }
        const double denominator = b.y - a.y;
        if (denominator == 0.0) {
            continue;
        }
        const double x = a.x + (point.y - a.y) * (b.x - a.x) / denominator;
        if (point.x < x) {
            inside = !inside;
        }
    }
    return inside;
}

double distanceToRectBorder(const RectD& rect, const Vec2d& point) noexcept {
    if (rect.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    const double dx = std::fmin(std::abs(point.x - rect.left()), std::abs(point.x - rect.right()));
    const double dy = std::fmin(std::abs(point.y - rect.top()), std::abs(point.y - rect.bottom()));
    return std::fmin(dx, dy);
}

}  // namespace kiriyama::core
