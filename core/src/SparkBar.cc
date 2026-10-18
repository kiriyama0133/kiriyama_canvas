#include "kiriyama/core/SparkBar.h"

#include <algorithm>
#include <cmath>

#include "kiriyama/math/MathUtils.h"

namespace kiriyama::core {

void SparkBarData::clear() {
    series.clear();
    tags.clear();
    values.clear();
}

double SparkBarData::valueAt(std::size_t tagIndex, std::size_t seriesIndex) const noexcept {
    if (tagIndex >= values.size()) {
        return 0.0;
    }
    const std::vector<double>& row = values[tagIndex];
    if (seriesIndex >= row.size()) {
        return 0.0;
    }
    const double value = row[seriesIndex];
    return (std::isfinite(value) && value > 0.0) ? value : 0.0;
}

double SparkBarData::totalAt(std::size_t tagIndex) const noexcept {
    if (tagIndex >= values.size()) {
        return 0.0;
    }
    double total = 0.0;
    for (std::size_t s = 0; s < seriesCount(); ++s) {
        total += valueAt(tagIndex, s);
    }
    return total;
}

double SparkBarData::maxTotal() const noexcept {
    double best = 0.0;
    for (std::size_t t = 0; t < tagCount(); ++t) {
        best = std::max(best, totalAt(t));
    }
    return best;
}

// ---------------------------------------------------------------- 布局

SparkBarLayout computeSparkBarLayout(Vec2d size, const SparkBarData& data,
                                     const SparkBarOptions& options) {
    SparkBarLayout layout;

    const double canvasWidth = std::max(0.0, size.x);
    const double canvasHeight = std::max(0.0, size.y);
    if (data.empty() || canvasWidth <= 0.0 || canvasHeight <= 0.0) {
        return layout;
    }

    const std::size_t count = data.tagCount();
    const double tagHeight =
        options.showTags ? (options.tagHeight > 0.0 ? options.tagHeight : options.tagFontSize * 1.5)
                         : 0.0;
    const double tagBlock = tagHeight > 0.0 ? tagHeight + options.tagGap : 0.0;

    // 柱子能用的高度。
    const double plotHeight =
        std::max(0.0, canvasHeight - tagBlock) * math::saturate(options.heightRatio);

    // 柱宽：显式给了就用，否则均分（至少留 1px，避免除零）。
    double barWidth = options.barWidth;
    if (!(barWidth > 0.0)) {
        const double gaps = options.barGap * static_cast<double>(count > 0 ? count - 1 : 0);
        barWidth = std::max(1.0, (canvasWidth - gaps) / static_cast<double>(count));
    }
    const double pitch = barWidth + options.barGap;

    // 整体水平居中（柱宽固定、画布更宽时）。
    const double contentWidth = pitch * static_cast<double>(count) - options.barGap;
    const double startX = std::max(0.0, (canvasWidth - contentWidth) * 0.5);

    const double maxTotal = data.maxTotal();
    const double unit = maxTotal > 0.0 ? plotHeight / maxTotal : 0.0;
    const double baseY = plotHeight;  // 基线（自下而上堆叠）

    for (std::size_t t = 0; t < count; ++t) {
        SparkBarItem item;
        item.tagIndex = t;

        const double x = startX + pitch * static_cast<double>(t);
        const double total = data.totalAt(t);
        const double barHeight = total * unit;

        item.barRect = RectD{x, baseY - barHeight, barWidth, barHeight};
        if (options.showTags) {
            item.tagRect = RectD{x, baseY + options.tagGap, barWidth, tagHeight};
        }

        // 自下而上堆叠：先画的在底部，段之间留 segmentGap 的缝。
        double cursor = baseY;
        for (std::size_t s = 0; s < data.seriesCount(); ++s) {
            const double value = data.valueAt(t, s);
            if (!(value > 0.0)) {
                continue;
            }
            const double rawHeight = value * unit;
            // 缝隙从段底扣掉，但保证每段至少 1px，否则细段会消失。
            const double gap = rawHeight > options.segmentGap + 1.0 ? options.segmentGap : 0.0;
            const double height = std::max(1.0, rawHeight - gap);

            SparkBarSegment segment;
            segment.value = value;
            segment.seriesIndex = s;
            segment.rect = RectD{x, cursor - height, barWidth, height};
            item.segments.push_back(segment);

            cursor -= rawHeight;
        }

        layout.items.push_back(std::move(item));
    }

    layout.valid = true;
    return layout;
}

}  // namespace kiriyama::core
