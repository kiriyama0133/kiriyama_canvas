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
    const double fontSize = options.tagFontSize > 0.0 ? options.tagFontSize : 12.0;
    const double tagHeight =
        options.showTags ? (options.tagHeight > 0.0 ? options.tagHeight : fontSize * 1.5) : 0.0;
    const double tagBlock = options.showTags ? (tagHeight + options.tagGap) : 0.0;

    // 统计全局各语言（series）总量，用来排底部 GitHub 风格圆点图例。
    std::vector<double> seriesTotals(data.seriesCount(), 0.0);
    double grandTotal = 0.0;
    std::size_t activeSeriesCount = 0;
    for (std::size_t s = 0; s < data.seriesCount(); ++s) {
        double sum = 0.0;
        for (std::size_t t = 0; t < count; ++t) {
            sum += data.valueAt(t, s);
        }
        seriesTotals[s] = sum;
        grandTotal += sum;
        if (sum > 0.0) {
            ++activeSeriesCount;
        }
    }

    // 预估底部图例所需高度（showTags 开启且有正值系列时显示）。
    const double legendRowHeight = fontSize * 1.6;
    const double legendRowGap = 6.0;
    const double legendTopGap = options.showTags && activeSeriesCount > 0 ? 18.0 : 0.0;
    double estimatedLegendHeight = 0.0;
    if (options.showTags && activeSeriesCount > 0) {
        double cursorX = 0.0;
        int legendRows = 1;
        for (std::size_t s = 0; s < data.seriesCount(); ++s) {
            if (!(seriesTotals[s] > 0.0)) {
                continue;
            }
            const double nameWidth =
                static_cast<double>(data.series[s].name.size()) * fontSize * 0.58;
            const double itemWidth = 10.0 + 6.0 + nameWidth + fontSize * 3.8 + 22.0;
            if (cursorX > 0.0 && cursorX + itemWidth > canvasWidth) {
                ++legendRows;
                cursorX = 0.0;
            }
            cursorX += itemWidth;
        }
        estimatedLegendHeight =
            legendTopGap + static_cast<double>(legendRows) * legendRowHeight +
            static_cast<double>(legendRows - 1) * legendRowGap;
    }

    const double usableHeight =
        std::max(0.0, canvasHeight - estimatedLegendHeight) * math::saturate(options.heightRatio);
    const double totalBarGaps = options.barGap * static_cast<double>(count > 0 ? count - 1 : 0);
    const double totalTagBlocks = tagBlock * static_cast<double>(count);

    // 横向条厚度（高度）：显式给了 barWidth 就用，否则按可用高度自适应。
    double barThickness = options.barWidth;
    if (!(barThickness > 0.0)) {
        const double raw =
            (usableHeight - totalBarGaps - totalTagBlocks) / static_cast<double>(count);
        barThickness = options.showTags ? std::clamp(raw, 10.0, 22.0) : std::max(1.0, raw);
    }

    const double rowPitch = tagBlock + barThickness + options.barGap;
    const double barsContentHeight =
        rowPitch * static_cast<double>(count) - options.barGap + estimatedLegendHeight;
    const double startY = std::max(0.0, (canvasHeight - barsContentHeight) * 0.5);

    for (std::size_t t = 0; t < count; ++t) {
        SparkBarItem item;
        item.tagIndex = t;

        const double rowTop = startY + rowPitch * static_cast<double>(t);
        if (options.showTags) {
            item.tagRect = RectD{0.0, rowTop, canvasWidth, tagHeight};
        }
        const double barTop = rowTop + tagBlock;
        const double total = data.totalAt(t);

        if (total > 0.0) {
            item.barRect = RectD{0.0, barTop, canvasWidth, barThickness};

            // 找出本行最后一个有效 series，末段右侧直接贴齐画布右边缘，不留尾巴缝隙。
            std::size_t lastValidSeries = 0;
            for (std::size_t s = 0; s < data.seriesCount(); ++s) {
                if (data.valueAt(t, s) > 0.0) {
                    lastValidSeries = s;
                }
            }

            // 自左向右按占比堆叠，相邻段之间留 segmentGap 缝隙。
            double cursorX = 0.0;
            for (std::size_t s = 0; s < data.seriesCount(); ++s) {
                const double value = data.valueAt(t, s);
                if (!(value > 0.0)) {
                    continue;
                }
                const double fraction = value / total;
                const double rawWidth = fraction * canvasWidth;
                const bool isLast = (s == lastValidSeries);
                const double gap =
                    (!isLast && rawWidth > options.segmentGap + 1.0) ? options.segmentGap : 0.0;
                const double width = isLast ? std::max(1.0, canvasWidth - cursorX)
                                            : std::max(1.0, rawWidth - gap);

                SparkBarSegment segment;
                segment.value = value;
                segment.fraction = fraction;
                segment.seriesIndex = s;
                segment.rect = RectD{cursorX, barTop, width, barThickness};
                item.segments.push_back(segment);

                cursorX += rawWidth;
            }
        }

        layout.items.push_back(std::move(item));
    }

    // 底部 GitHub 风格语言占比图例：● C++ 68.4%   ● JavaScript 15.2% ...
    if (options.showTags && grandTotal > 0.0) {
        const double dotSize = std::clamp(fontSize * 0.78, 8.0, 12.0);
        double cursorX = 0.0;
        double cursorY =
            startY + rowPitch * static_cast<double>(count) - options.barGap + legendTopGap;

        for (std::size_t s = 0; s < data.seriesCount(); ++s) {
            const double sum = seriesTotals[s];
            if (!(sum > 0.0)) {
                continue;
            }
            const double fraction = sum / grandTotal;
            const double nameWidth =
                static_cast<double>(data.series[s].name.size()) * fontSize * 0.58;
            const double textWidth = std::max(36.0, nameWidth + fontSize * 3.8);
            const double itemWidth = dotSize + 6.0 + textWidth;

            if (cursorX > 0.0 && cursorX + itemWidth > canvasWidth) {
                cursorX = 0.0;
                cursorY += legendRowHeight + legendRowGap;
            }

            SparkBarLegendItem legendItem;
            legendItem.seriesIndex = s;
            legendItem.value = sum;
            legendItem.fraction = fraction;
            legendItem.dotRect =
                RectD{cursorX, cursorY + (legendRowHeight - dotSize) * 0.5, dotSize, dotSize};
            legendItem.textRect =
                RectD{cursorX + dotSize + 6.0, cursorY, textWidth, legendRowHeight};
            legendItem.hitRect = RectD{cursorX, cursorY, itemWidth, legendRowHeight};
            layout.legend.push_back(legendItem);

            cursorX += itemWidth + 20.0;
        }
    }

    layout.valid = true;
    return layout;
}

SparkBarPick hitTestSparkBar(const SparkBarLayout& layout, Vec2d localPoint) noexcept {
    SparkBarPick pick;
    for (std::size_t i = 0; i < layout.items.size(); ++i) {
        const SparkBarItem& item = layout.items[i];
        const bool onBar = !item.barRect.empty() && item.barRect.contains(localPoint);
        const bool onTag = !item.tagRect.empty() && item.tagRect.contains(localPoint);
        if (!onBar && !onTag) {
            continue;
        }
        pick.item = i;
        for (std::size_t s = 0; s < item.segments.size(); ++s) {
            if (item.segments[s].rect.contains(localPoint)) {
                pick.segment = s;
                break;
            }
        }
        return pick;
    }
    for (const SparkBarLegendItem& leg : layout.legend) {
        if (!leg.hitRect.empty() && leg.hitRect.contains(localPoint)) {
            pick.legendSeries = leg.seriesIndex;
            return pick;
        }
    }
    return pick;
}

}  // namespace kiriyama::core
