#pragma once

// 图表几何基础：像素矩形、线段、圆弧。
// 布局、柱形、饼图扇区、标签碰撞、命中测试都建立在这几个类型上。
//
// 坐标约定：像素坐标系原点在画布左上角，y 向下为正（与 SDL / Canvas 一致）。
// 数据坐标到像素坐标的翻转由 core::Axis 的 pixelRange 方向决定，这里不做假设。

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

#include "kiriyama/math/MathUtils.h"
#include "kiriyama/math/Transform2D.h"
#include "kiriyama/math/Vector2D.h"

namespace kiriyama::core {

using math::Vec2d;

/// 无效索引，命中测试与饼图扇区都用它表示"没有"。
inline constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

/// 轴对齐矩形。width / height 允许为 0，负值请用 normalized() 修正。
struct RectD {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    constexpr RectD() noexcept = default;
    constexpr RectD(double xValue, double yValue, double widthValue, double heightValue) noexcept
        : x(xValue), y(yValue), width(widthValue), height(heightValue) {}

    [[nodiscard]] static constexpr RectD FromLTRB(double left, double top, double right,
                                                 double bottom) noexcept {
        return RectD{left, top, right - left, bottom - top}.normalized();
    }

    [[nodiscard]] constexpr double left() const noexcept { return x; }
    [[nodiscard]] constexpr double top() const noexcept { return y; }
    [[nodiscard]] constexpr double right() const noexcept { return x + width; }
    [[nodiscard]] constexpr double bottom() const noexcept { return y + height; }

    [[nodiscard]] constexpr Vec2d origin() const noexcept { return {x, y}; }
    [[nodiscard]] constexpr Vec2d center() const noexcept {
        return {x + width * 0.5, y + height * 0.5};
    }
    [[nodiscard]] constexpr Vec2d size() const noexcept { return {width, height}; }
    [[nodiscard]] constexpr double area() const noexcept { return width * height; }

    /// 宽或高任一为 0 即视为空（没有可绘制/可碰撞的面积）。
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0.0 || height <= 0.0; }

    [[nodiscard]] constexpr RectD normalized() const noexcept {
        const double nx = width < 0.0 ? x + width : x;
        const double ny = height < 0.0 ? y + height : y;
        return {nx, ny, width < 0.0 ? -width : width, height < 0.0 ? -height : height};
    }

    /// 点是否落在矩形内，边界算命中。
    [[nodiscard]] constexpr bool contains(const Vec2d& point) const noexcept {
        return point.x >= left() && point.x <= right() && point.y >= top() &&
               point.y <= bottom();
    }

    [[nodiscard]] constexpr bool contains(const RectD& other) const noexcept {
        return other.left() >= left() && other.right() <= right() && other.top() >= top() &&
               other.bottom() <= bottom();
    }

    /// 严格重叠：仅共边不算（标签碰撞判定需要这个语义）。
    [[nodiscard]] constexpr bool intersects(const RectD& other) const noexcept {
        return left() < other.right() && other.left() < right() && top() < other.bottom() &&
               other.top() < bottom();
    }

    [[nodiscard]] constexpr RectD intersection(const RectD& other) const noexcept {
        if (!intersects(other)) {
            return {};
        }
        return FromLTRB(math::clamp(left(), other.left(), other.right()),
                        math::clamp(top(), other.top(), other.bottom()),
                        math::clamp(right(), other.left(), other.right()),
                        math::clamp(bottom(), other.top(), other.bottom()));
    }

    [[nodiscard]] constexpr RectD united(const RectD& other) const noexcept {
        if (empty()) {
            return other;
        }
        if (other.empty()) {
            return *this;
        }
        return FromLTRB(std::fmin(left(), other.left()), std::fmin(top(), other.top()),
                        std::fmax(right(), other.right()), std::fmax(bottom(), other.bottom()));
    }

    /// 向内收缩 d 像素；d 为负时向外扩张。收缩到负尺寸时塌缩成中心点上的空矩形。
    [[nodiscard]] constexpr RectD inset(double d) const noexcept {
        const double halfWidth = width * 0.5;
        const double halfHeight = height * 0.5;
        const double newHalfWidth = halfWidth - d > 0.0 ? halfWidth - d : 0.0;
        const double newHalfHeight = halfHeight - d > 0.0 ? halfHeight - d : 0.0;
        return {x + halfWidth - newHalfWidth, y + halfHeight - newHalfHeight, newHalfWidth * 2.0,
                newHalfHeight * 2.0};
    }

    [[nodiscard]] constexpr RectD expanded(double d) const noexcept { return inset(-d); }

    [[nodiscard]] constexpr RectD translated(const Vec2d& offset) const noexcept {
        return {x + offset.x, y + offset.y, width, height};
    }

    /// 把矩形夹到 bounds 内；越界部分被裁掉，完全在外时返回空矩形。
    [[nodiscard]] constexpr RectD clampedTo(const RectD& bounds) const noexcept {
        return intersection(bounds);
    }

    /// 点到矩形的最近距离，点在内部为 0。
    [[nodiscard]] double distanceTo(const Vec2d& point) const noexcept {
        const double dx = point.x < left() ? left() - point.x
                                           : (point.x > right() ? point.x - right() : 0.0);
        const double dy = point.y < top() ? top() - point.y
                                         : (point.y > bottom() ? point.y - bottom() : 0.0);
        return std::sqrt(dx * dx + dy * dy);
    }

    [[nodiscard]] constexpr bool operator==(const RectD& rhs) const noexcept = default;
};

using RectF = RectD;  // 布局统一用 double，保留别名方便调用方

// ---------------------------------------------------------------- 线段

/// 点到线段 ab 的最近点。ab 退化为点时返回 a。
[[nodiscard]] inline Vec2d closestPointOnSegment(const Vec2d& point, const Vec2d& a,
                                                 const Vec2d& b) noexcept {
    const Vec2d ab = b - a;
    const double lengthSquared = ab.lengthSquared();
    if (!(lengthSquared > 0.0)) {
        return a;
    }
    const double t = math::saturate((point - a).dot(ab) / lengthSquared);
    return a + ab * t;
}

[[nodiscard]] inline double distanceToSegment(const Vec2d& point, const Vec2d& a,
                                              const Vec2d& b) noexcept {
    return (point - closestPointOnSegment(point, a, b)).length();
}

/// 折线最近距离。空折线返回 +inf，单点折线退化为点距离。
[[nodiscard]] inline double distanceToPolyline(const Vec2d& point,
                                               std::span<const Vec2d> points) noexcept {
    if (points.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    if (points.size() == 1) {
        return (point - points.front()).length();
    }
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double d = distanceToSegment(point, points[i - 1], points[i]);
        if (d < best) {
            best = d;
        }
    }
    return best;
}

/// 两条线段是否相交（含端点接触）。
[[nodiscard]] inline bool segmentsIntersect(const Vec2d& a1, const Vec2d& a2, const Vec2d& b1,
                                            const Vec2d& b2, double epsilon = 1e-12) noexcept {
    const Vec2d r = a2 - a1;
    const Vec2d s = b2 - b1;
    const double denominator = r.cross(s);
    if (math::nearlyZero(denominator, epsilon)) {
        return false;  // 平行或共线：碰撞检测里按不相交处理，避免误判
    }
    const Vec2d delta = b1 - a1;
    const double t = delta.cross(s) / denominator;
    const double u = delta.cross(r) / denominator;
    return t >= -epsilon && t <= 1.0 + epsilon && u >= -epsilon && u <= 1.0 + epsilon;
}

// ---------------------------------------------------------------- 圆 / 圆弧

struct CircleD {
    Vec2d center{};
    double radius{0.0};

    [[nodiscard]] constexpr bool contains(const Vec2d& point) const noexcept {
        return (point - center).lengthSquared() <= radius * radius;
    }
};

/// 把角度折回 [0, 2pi)。
[[nodiscard]] inline double normalizeAnglePositive(double radians) noexcept {
    double wrapped = std::fmod(radians, math::kTwoPi);
    if (wrapped < 0.0) {
        wrapped += math::kTwoPi;
    }
    return wrapped;
}

/// 环形扇区。屏幕坐标下 sweepAngle 为正表示顺时针（y 轴向下）。
struct ArcD {
    Vec2d center{};
    double innerRadius{0.0};
    double outerRadius{0.0};
    double startAngle{0.0};
    double sweepAngle{0.0};

    [[nodiscard]] Vec2d pointAt(double angle, double radius) const noexcept {
        return {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }

    [[nodiscard]] double midAngle() const noexcept { return startAngle + sweepAngle * 0.5; }

    [[nodiscard]] bool containsAngle(double angle, double epsilon = 1e-9) const noexcept {
        if (std::abs(sweepAngle) >= math::kTwoPi - epsilon) {
            return true;
        }
        if (sweepAngle >= 0.0) {
            return normalizeAnglePositive(angle - startAngle) <= sweepAngle + epsilon;
        }
        return normalizeAnglePositive(startAngle - angle) <= -sweepAngle + epsilon;
    }

    /// radiusTolerance 允许把外半径放大、内半径缩小，便于点击小扇区。
    [[nodiscard]] bool contains(const Vec2d& point,
                                double radiusTolerance = 0.0) const noexcept {
        const double distance = (point - center).length();
        if (distance > outerRadius + radiusTolerance) {
            return false;
        }
        if (distance < innerRadius - radiusTolerance) {
            return false;
        }
        return containsAngle(std::atan2(point.y - center.y, point.x - center.x));
    }
};

// ---------------------------------------------------------------- 多边形 / 变换

/// 变换后包围盒：把矩形四角变换过去再取轴对齐包围盒。
/// 零宽度 / 零高度（水平线、细条）照常变换——位置本身是有效信息，
/// 命中测试的剪枝依赖它。
[[nodiscard]] RectD transformedBounds(const math::Affine2D& transform,
                                      const RectD& rect) noexcept;

/// 多边形是否包含某点（射线法，边界算命中）。
[[nodiscard]] bool polygonContains(std::span<const Vec2d> points, const Vec2d& point) noexcept;

/// 点到矩形边框的距离（内部也返回到最近边的距离）。
[[nodiscard]] double distanceToRectBorder(const RectD& rect, const Vec2d& point) noexcept;

// ---------------------------------------------------------------- 标签碰撞

/// 贪心去重叠：按传入顺序保留标签，后到的与前序已保留标签（外扩 gap）冲突则隐藏。
/// 返回与输入等长的可见性标记；空矩形始终可见（它不占位置）。
[[nodiscard]] std::vector<bool> resolveLabelOverlaps(std::span<const RectD> boxes,
                                                     double gap = 2.0);

}  // namespace kiriyama::core
