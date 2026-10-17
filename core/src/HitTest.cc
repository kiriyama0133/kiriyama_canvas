#include "kiriyama/core/HitTest.h"

#include <algorithm>
#include <cmath>

namespace kiriyama::core {
namespace {

/// 折线命中时吸附到更近的那个顶点，tooltip 才有意义。
std::size_t nearestEndpoint(const DataPoint& a, std::size_t indexA, const DataPoint& b,
                            std::size_t indexB, const ChartLayout& layout, const Vec2d& pixel,
                            Vec2d& outPosition) {
    const Vec2d positionA = layout.toPixel(a.x, a.y);
    const Vec2d positionB = layout.toPixel(b.x, b.y);
    if ((pixel - positionA).lengthSquared() <= (pixel - positionB).lengthSquared()) {
        outPosition = positionA;
        return indexA;
    }
    outPosition = positionB;
    return indexB;
}

}  // namespace

HitResult hitTestPolyline(const Dataset& dataset, const ChartLayout& layout, const Vec2d& pixel,
                          const HitTestOptions& options) {
    HitResult best;
    if (!layout.valid) {
        return best;
    }

    for (std::size_t s = 0; s < dataset.series.size(); ++s) {
        const std::vector<DataPoint>& points = dataset.series[s].points;
        if (points.empty()) {
            continue;
        }

        if (points.size() == 1) {
            if (!points[0].valid()) {
                continue;
            }
            const Vec2d position = layout.toPixel(points[0].x, points[0].y);
            const double distance = (pixel - position).length();
            if (distance < best.distance) {
                best.distance = distance;
                best.seriesIndex = s;
                best.pointIndex = 0;
                best.position = position;
                best.value = points[0].y;
            }
            continue;
        }

        for (std::size_t i = 1; i < points.size(); ++i) {
            // 任一端点缺失就断开线段，绝不跨过空值连线。
            if (!points[i - 1].valid() || !points[i].valid()) {
                continue;
            }

            const Vec2d a = layout.toPixel(points[i - 1].x, points[i - 1].y);
            const Vec2d b = layout.toPixel(points[i].x, points[i].y);
            const double distance = distanceToSegment(pixel, a, b);
            if (distance < best.distance) {
                best.distance = distance;
                best.seriesIndex = s;
                best.pointIndex = nearestEndpoint(points[i - 1], i - 1, points[i], i, layout, pixel,
                                                 best.position);
                best.value = points[best.pointIndex].y;
            }
        }
    }

    best.hit = best.distance <= std::max(0.0, options.tolerance);
    return best;
}

HitResult hitTestVertices(const Dataset& dataset, const ChartLayout& layout, const Vec2d& pixel,
                          const HitTestOptions& options) {
    HitResult best;
    if (!layout.valid) {
        return best;
    }

    for (std::size_t s = 0; s < dataset.series.size(); ++s) {
        const std::vector<DataPoint>& points = dataset.series[s].points;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (!points[i].valid()) {
                continue;
            }
            const Vec2d position = layout.toPixel(points[i].x, points[i].y);
            const double distance = (pixel - position).length();
            if (distance < best.distance) {
                best.distance = distance;
                best.seriesIndex = s;
                best.pointIndex = i;
                best.position = position;
                best.value = points[i].y;
            }
        }
    }

    best.hit = best.distance <= std::max(0.0, options.tolerance);
    return best;
}

HitResult hitTestBars(const Dataset& dataset, const ChartLayout& layout, BarMode mode,
                      const Vec2d& pixel, const HitTestOptions& options) {
    HitResult result;
    if (!layout.valid) {
        return result;
    }

    const std::vector<BarRect> rects = computeBarRects(dataset, layout, mode);
    const double tolerance = std::max(0.0, options.tolerance);

    // 逆序遍历：堆叠时后画的系列压在上面，应该优先命中。
    for (auto it = rects.rbegin(); it != rects.rend(); ++it) {
        if (!it->rect.expanded(tolerance).contains(pixel)) {
            continue;
        }
        result.hit = true;
        result.seriesIndex = it->seriesIndex;
        result.categoryIndex = it->categoryIndex;
        result.pointIndex = it->categoryIndex;
        result.distance = it->rect.contains(pixel) ? 0.0 : it->rect.distanceTo(pixel);
        result.position = pixel;
        result.value = it->value;
        return result;
    }
    return result;
}

HitResult hitTestPie(const PieChartData& data, const PieLayout& layout, const Vec2d& pixel,
                     const HitTestOptions& options) {
    HitResult result;
    (void)data;
    if (!layout.valid) {
        return result;
    }

    const Vec2d delta = pixel - layout.center;
    const double radius = delta.length();
    const double tolerance = std::max(0.0, options.tolerance);
    if (radius > layout.outerRadius + tolerance || radius < layout.innerRadius - tolerance) {
        return result;
    }

    const double angle = std::atan2(delta.y, delta.x);
    for (const PieSector& sector : layout.sectors) {
        const ArcD arc{layout.center, layout.innerRadius, layout.outerRadius, sector.startAngle,
                       sector.sweepAngle};
        if (!arc.containsAngle(angle)) {
            continue;
        }
        result.hit = true;
        result.sliceIndex = sector.sliceIndex;
        result.seriesIndex = 0;
        result.distance = 0.0;
        result.position = pixel;
        result.value = sector.value;
        result.fraction = sector.fraction;
        return result;
    }
    return result;
}

HitResult hitTestCategory(const Dataset& dataset, const ChartLayout& layout, const Vec2d& pixel) {
    HitResult result;
    if (!layout.valid || !layout.plot.contains(pixel)) {
        return result;
    }

    const std::size_t count = dataset.categoryCount();
    if (count == 0) {
        return result;
    }

    std::size_t bestIndex = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    double bestPosition = layout.plot.center().x;
    for (std::size_t i = 0; i < count; ++i) {
        const double position = layout.toPixelX(static_cast<double>(i));
        const double distance = std::abs(position - pixel.x);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
            bestPosition = position;
        }
    }

    result.hit = true;
    result.categoryIndex = bestIndex;
    result.pointIndex = bestIndex;
    result.distance = bestDistance;
    result.position = {bestPosition, math::clamp(pixel.y, layout.plot.top(), layout.plot.bottom())};
    return result;
}

std::size_t nearestCategoryIndex(const Dataset& dataset, double xValue) {
    std::size_t bestIndex = kInvalidIndex;
    double bestDistance = std::numeric_limits<double>::infinity();

    for (const Series& series : dataset.series) {
        for (std::size_t i = 0; i < series.points.size(); ++i) {
            const DataPoint& point = series.points[i];
            if (!point.valid()) {
                continue;
            }
            const double distance = std::abs(point.x - xValue);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = i;
            }
        }
    }
    return bestIndex;
}

}  // namespace kiriyama::core
