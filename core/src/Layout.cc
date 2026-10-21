#include "kiriyama/core/Layout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "kiriyama/math/Ticks.h"

namespace kiriyama::core {
namespace {

constexpr double kLabelMinGap = 6.0;

struct DraftAxis {
    ScaleType scale{ScaleType::Linear};
    RangeD domain{0.0, 1.0};
    std::vector<double> values;
    /// 与 values 一一对应；为空则按 format 生成。
    std::vector<std::string> labels;
    double step{1.0};
};

[[nodiscard]] RangeD ensureSpan(RangeD range) {
    if (range.empty()) {
        return RangeD{0.0, 1.0};
    }
    if (!range.degenerate()) {
        return range;
    }
    const double center = range.minimum();
    const double padding = center == 0.0 ? 1.0 : std::abs(center) * 0.5;
    return RangeD{center - padding, center + padding};
}

/// 线性轴草稿：nice domain + 等分刻度，保证刻度落在 1/2/5 × 10^n 上。
[[nodiscard]] DraftAxis draftLinear(RangeD data, const AxisOptions& options, int count) {
    DraftAxis draft;
    draft.scale = ScaleType::Linear;

    RangeD domain = data;
    if (options.includeZero) {
        domain.expandToInclude(0.0);
    }
    domain = ensureSpan(domain);

    if (options.nice) {
        domain = ensureSpan(math::niceDomain(domain, count, options.includeZero));

        double step = math::niceStep(domain.span(), count, /*roundToNearest=*/false);
        if (!(step > 0.0) || !std::isfinite(step)) {
            step = domain.span();
        }
        if (step > 0.0) {
            // 再吸附一次：确保 domain 端点是 step 的整数倍，刻度标签才干净。
            const double low = std::floor(domain.minimum() / step) * step;
            const double high = std::ceil(domain.maximum() / step) * step;
            domain = RangeD{low, high};
        }

        int intervals = static_cast<int>(std::llround(domain.span() / step));
        intervals = std::clamp(intervals, 1, 64);
        draft.step = domain.span() / static_cast<double>(intervals);
        draft.values = evenTickValues(domain, intervals);
    } else {
        draft.values = math::linearTicks(domain, count);
        draft.step = draft.values.size() > 1 ? draft.values[1] - draft.values[0]
                                             : (domain.span() > 0.0 ? domain.span() : 1.0);
    }

    draft.domain = domain;
    return draft;
}

[[nodiscard]] DraftAxis draftLog(RangeD data, const AxisOptions& options, int count) {
    DraftAxis draft;
    draft.scale = ScaleType::Log;
    draft.domain = niceLogDomain(data);
    draft.values = logTickValues(draft.domain, count);
    draft.step = draft.values.size() > 1 ? draft.values[1] - draft.values[0]
                                         : (draft.domain.span() > 0.0 ? draft.domain.span() : 1.0);
    return draft;
}

/// 类目轴：domain 固定为 [-0.5, n-0.5]，刻度落在整数索引上。
[[nodiscard]] DraftAxis draftCategory(const std::vector<std::string>& labels, int stride) {
    DraftAxis draft;
    draft.scale = ScaleType::Linear;
    const std::size_t count = labels.size();
    draft.domain = RangeD{-0.5, static_cast<double>(count) - 0.5};
    draft.step = 1.0;

    const int step = std::max(1, stride);
    for (std::size_t i = 0; i < count; i += static_cast<std::size_t>(step)) {
        draft.values.push_back(static_cast<double>(i));
        draft.labels.push_back(labels[i]);
    }
    return draft;
}

/// 文本轴自定义刻度（用于折线图 X/Y 轴刻度 Text）：在 domain 上均匀分布 labels，支持 stride 抽稀。
void applyCustomTickLabels(DraftAxis& draft, const std::vector<std::string>& labels, int stride = 1) {
    if (labels.empty()) {
        return;
    }
    draft.values.clear();
    draft.labels.clear();
    const std::size_t count = labels.size();
    if (count == 1) {
        draft.values.push_back(draft.domain.midpoint());
        draft.labels.push_back(labels.front());
        draft.step = draft.domain.span() > 0.0 ? draft.domain.span() : 1.0;
        return;
    }
    const int step = std::max(1, stride);
    draft.step = draft.domain.span() / static_cast<double>(count - 1);
    for (std::size_t i = 0; i < count; i += static_cast<std::size_t>(step)) {
        const double ratio = static_cast<double>(i) / static_cast<double>(count - 1);
        draft.values.push_back(draft.domain.minimum() + draft.domain.span() * ratio);
        draft.labels.push_back(labels[i]);
    }
}

[[nodiscard]] std::string tickLabel(const DraftAxis& draft, const AxisOptions& options,
                                    std::size_t index) {
    if (index < draft.labels.size()) {
        return draft.labels[index];
    }
    return options.format.format(draft.values[index], draft.step);
}

[[nodiscard]] TextMetrics measureSingleTick(std::string_view rawLabel, const AxisOptions& options,
                                            const TextMeasureFn& measure) {
    const std::string directed = formatTextDirection(rawLabel, options.labelDirection);
    const TextMetrics base = measure(directed, options.labelFontSize);
    return rotatedTextMetrics(base, options.labelAngle);
}

[[nodiscard]] TextMetrics measureTicks(const DraftAxis& draft, const AxisOptions& options,
                                       const TextMeasureFn& measure) {
    TextMetrics total{0.0, 0.0};
    for (std::size_t i = 0; i < draft.values.size(); ++i) {
        const TextMetrics one =
            measureSingleTick(tickLabel(draft, options, i), options, measure);
        total.width = std::max(total.width, one.width);
        total.height = std::max(total.height, one.height);
    }
    return total;
}

/// 标签在轴上是否会互相压住（或超出画布左右边缘）。
[[nodiscard]] bool tickLabelsFit(const DraftAxis& draft, const AxisOptions& options,
                                 const TextMeasureFn& measure, const RectD& plot,
                                 double canvasWidth) {
    if (draft.values.size() < 2) {
        return true;
    }

    const Axis axis(draft.scale, draft.domain, RangeD{plot.left(), plot.right()});
    std::vector<double> widths;
    widths.reserve(draft.values.size());
    double previousRight = -std::numeric_limits<double>::infinity();

    for (std::size_t i = 0; i < draft.values.size(); ++i) {
        const double width =
            measureSingleTick(tickLabel(draft, options, i), options, measure).width;
        widths.push_back(width);

        const double position = axis.map(draft.values[i]);
        const double left = position - width * 0.5;

        if (i == 0 && left < 0.0) {
            return false;
        }
        if (i + 1 == draft.values.size() && position + width * 0.5 > canvasWidth) {
            return false;
        }
        if (left < previousRight + kLabelMinGap) {
            return false;
        }
        previousRight = position + width * 0.5;
    }
    return true;
}

[[nodiscard]] int intervalsOf(RangeD domain, int target) {
    if (!(domain.span() > 0.0)) {
        return 1;
    }
    const double step = math::niceStep(domain.span(), std::max(1, target), false);
    if (!(step > 0.0) || !std::isfinite(step)) {
        return 1;
    }
    return std::clamp(static_cast<int>(std::llround(domain.span() / step)), 1, 20);
}

/// 找最小的分段数 n（>= start），使两个轴都能用"最接近的 nice step"覆盖各自 span。
/// 直接对 span/n 向上取整会出现 22 -> 50 这种过度扩张，让数据只占轴的一半。
[[nodiscard]] int findAlignedIntervals(RangeD a, RangeD b, int start) {
    constexpr int kMaxIntervals = 12;
    for (int n = std::max(1, start); n <= kMaxIntervals; ++n) {
        const double stepA = math::niceNumber(a.span() / static_cast<double>(n),
                                              /*roundToNearest=*/true);
        const double stepB = math::niceNumber(b.span() / static_cast<double>(n),
                                              /*roundToNearest=*/true);
        if (!(stepA > 0.0) || !(stepB > 0.0)) {
            continue;
        }
        if (stepA * n >= a.span() * (1.0 - 1e-12) &&
            stepB * n >= b.span() * (1.0 - 1e-12)) {
            return n;
        }
    }
    return std::max(1, start);
}

/// 把 domain 扩成正好 intervals 段 step，尽量保住 0 基线。
[[nodiscard]] RangeD expandToIntervals(RangeD range, double step, int intervals) {
    const double span = step * static_cast<double>(intervals);
    if (!(span > 0.0) || !std::isfinite(span)) {
        return range;
    }

    const double tolerance = std::abs(step) * 1e-9 + 1e-12;
    if (std::abs(range.minimum()) <= tolerance) {
        return RangeD{0.0, span};
    }
    if (std::abs(range.maximum()) <= tolerance) {
        return RangeD{-span, 0.0};
    }

    const double center = range.midpoint();
    return RangeD{center - span * 0.5, center + span * 0.5};
}

[[nodiscard]] AxisLayout finalizeAxis(const DraftAxis& draft, const AxisOptions& options,
                                      AxisPosition position, RangeD pixelRange,
                                      const TextMeasureFn& measure) {
    AxisLayout out;
    out.position = position;
    out.scale = draft.scale;
    out.domain = draft.domain;
    out.pixelRange = pixelRange;
    out.visible = options.visible;
    out.title = options.title;
    out.axis = Axis(draft.scale, draft.domain, pixelRange);

    if (!options.visible) {
        return out;
    }

    out.ticks.reserve(draft.values.size());
    for (std::size_t i = 0; i < draft.values.size(); ++i) {
        TickMark mark;
        mark.value = draft.values[i];
        mark.position = out.axis.map(mark.value);
        mark.label = tickLabel(draft, options, i);

        const TextMetrics metrics = measureSingleTick(mark.label, options, measure);
        out.labelWidth = std::max(out.labelWidth, metrics.width);
        out.labelHeight = std::max(out.labelHeight, metrics.height);
        out.ticks.push_back(std::move(mark));
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------- 文本测量

TextMetrics defaultTextMeasure(std::string_view text, double fontSize) {
    if (!(fontSize > 0.0)) {
        return {0.0, 0.0};
    }

    double maxWidth = 0.0;
    double lineWidth = 0.0;
    std::size_t lineCount = 1;
    for (const char raw : text) {
        if (raw == '\r') {
            continue;
        }
        if (raw == '\n') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0.0;
            ++lineCount;
            continue;
        }
        const auto byte = static_cast<unsigned char>(raw);
        if ((byte & 0xC0) == 0x80) {
            continue;  // UTF-8 续字节，宽度算在首字节上
        }
        if (byte >= 0x80) {
            lineWidth += fontSize;  // CJK / 全角
        } else if (byte == ' ') {
            lineWidth += fontSize * 0.3;
        } else if (byte == '.' || byte == ',' || byte == ':' || byte == ';' || byte == '|') {
            lineWidth += fontSize * 0.3;
        } else if (byte == '-' || byte == '+') {
            lineWidth += fontSize * 0.36;
        } else {
            lineWidth += fontSize * 0.6;
        }
    }
    maxWidth = std::max(maxWidth, lineWidth);
    return {maxWidth, static_cast<double>(lineCount) * fontSize * 1.25};
}

TextMetrics measureText(const TextMeasureFn& measure, std::string_view text, double fontSize) {
    if (text.empty()) {
        return {};
    }
    TextMetrics metrics =
        measure ? measure(text, fontSize) : defaultTextMeasure(text, fontSize);
    if (!(metrics.height > 0.0)) {
        metrics.height = fontSize * 1.25;
    }
    return metrics;
}

std::string formatTextDirection(std::string_view text, TextDirection direction) {
    if (direction == TextDirection::Horizontal || text.empty()) {
        return std::string{text};
    }

    std::string out;
    out.reserve(text.size() * 2);
    bool needNewline = false;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead == '\r') {
            ++i;
            continue;
        }
        if (lead == '\n') {
            out.push_back('\n');
            needNewline = false;
            ++i;
            continue;
        }
        std::size_t charBytes = 1;
        if ((lead & 0x80u) == 0u) {
            charBytes = 1;
        } else if ((lead & 0xE0u) == 0xC0u) {
            charBytes = 2;
        } else if ((lead & 0xF0u) == 0xE0u) {
            charBytes = 3;
        } else if ((lead & 0xF8u) == 0xF0u) {
            charBytes = 4;
        }
        charBytes = std::min(charBytes, text.size() - i);

        if (needNewline) {
            out.push_back('\n');
        }
        out.append(text.substr(i, charBytes));
        needNewline = true;
        i += charBytes;
    }
    return out;
}

TextMetrics rotatedTextMetrics(const TextMetrics& metrics, double angle) noexcept {
    if (std::abs(angle) <= 1e-6 || (metrics.width <= 0.0 && metrics.height <= 0.0)) {
        return metrics;
    }
    const double c = std::abs(std::cos(angle));
    const double s = std::abs(std::sin(angle));
    return {
        c * metrics.width + s * metrics.height,
        s * metrics.width + c * metrics.height,
    };
}

// ---------------------------------------------------------------- 多轴对齐

AlignedDomains alignYDomains(RangeD primary, RangeD secondary, int targetTickCount) noexcept {
    AlignedDomains result;
    result.primary = primary;
    result.secondary = secondary;
    result.intervals = 1;

    if (!(primary.span() > 0.0) || !(secondary.span() > 0.0)) {
        return result;
    }

    const int target = std::max(2, targetTickCount);
    const int start = std::max(intervalsOf(primary, target), intervalsOf(secondary, target));
    const int intervals = findAlignedIntervals(primary, secondary, start);

    // 分段数被上限截断时，最接近的 nice step 可能盖不住 span，退回到向上取整。
    auto coverStep = [](RangeD range, int count) {
        double step = math::niceNumber(range.span() / static_cast<double>(count),
                                       /*roundToNearest=*/true);
        if (!(step * count >= range.span())) {
            step = math::niceNumber(range.span() / static_cast<double>(count),
                                    /*roundToNearest=*/false);
        }
        return step;
    };

    const double stepPrimary = coverStep(primary, intervals);
    const double stepSecondary = coverStep(secondary, intervals);
    if (!(stepPrimary > 0.0) || !(stepSecondary > 0.0)) {
        return result;
    }

    result.primary = expandToIntervals(primary, stepPrimary, intervals);
    result.secondary = expandToIntervals(secondary, stepSecondary, intervals);
    result.intervals = intervals;
    return result;
}

// ---------------------------------------------------------------- 直角坐标布局

ChartLayout computeLayout(Vec2d canvasSize, const DataBounds& bounds,
                          const LayoutOptions& options) {
    return computeLayout(canvasSize, bounds, bounds, options);
}

ChartLayout computeLayout(Vec2d canvasSize, const DataBounds& bounds,
                          const DataBounds& secondaryBounds, const LayoutOptions& options) {
    ChartLayout layout;
    const double canvasWidth = std::max(0.0, canvasSize.x);
    const double canvasHeight = std::max(0.0, canvasSize.y);
    layout.canvas = RectD{0.0, 0.0, canvasWidth, canvasHeight};
    layout.hasSecondaryY = options.hasSecondaryY;

    const TextMeasureFn measure =
        options.measureText ? options.measureText : TextMeasureFn(defaultTextMeasure);

    // ---- y 轴（左）草稿
    const RangeD yData = bounds.hasData ? bounds.y : RangeD{0.0, 1.0};
    DraftAxis yDraft = options.y.scale == ScaleType::Log
                           ? draftLog(yData, options.y, options.y.targetTickCount)
                           : draftLinear(yData, options.y, options.y.targetTickCount);
    if (!options.y.tickLabels.empty()) {
        applyCustomTickLabels(yDraft, options.y.tickLabels);
    }

    // ---- y 轴（右）草稿
    DraftAxis secondaryDraft;
    if (options.hasSecondaryY) {
        const RangeD secondaryData =
            secondaryBounds.hasData ? secondaryBounds.y : RangeD{0.0, 1.0};
        secondaryDraft =
            options.secondaryY.scale == ScaleType::Log
                ? draftLog(secondaryData, options.secondaryY, options.secondaryY.targetTickCount)
                : draftLinear(secondaryData, options.secondaryY,
                              options.secondaryY.targetTickCount);

        // 两个线性轴才谈得上网格对齐；对数轴段数由数量级决定，不强行拉平。
        if (options.y.scale == ScaleType::Linear &&
            options.secondaryY.scale == ScaleType::Linear &&
            options.y.tickLabels.empty() && options.secondaryY.tickLabels.empty()) {
            const AlignedDomains aligned = alignYDomains(
                yDraft.domain, secondaryDraft.domain,
                std::max(options.y.targetTickCount, options.secondaryY.targetTickCount));

            yDraft.domain = aligned.primary;
            yDraft.values = evenTickValues(aligned.primary, aligned.intervals);
            yDraft.step = aligned.primary.span() / static_cast<double>(aligned.intervals);

            secondaryDraft.domain = aligned.secondary;
            secondaryDraft.values = evenTickValues(aligned.secondary, aligned.intervals);
            secondaryDraft.step =
                aligned.secondary.span() / static_cast<double>(aligned.intervals);
        }
        if (!options.secondaryY.tickLabels.empty()) {
            applyCustomTickLabels(secondaryDraft, options.secondaryY.tickLabels);
        }
    }

    // ---- 先用 y 标签宽度定出绘图区
    const TextMetrics yMetrics =
        options.y.visible ? measureTicks(yDraft, options.y, measure) : TextMetrics{};
    const TextMetrics secondaryMetrics =
        (options.hasSecondaryY && options.secondaryY.visible)
            ? measureTicks(secondaryDraft, options.secondaryY, measure)
            : TextMetrics{};

    double left = options.padding.left;
    if (options.y.visible) {
        left += yMetrics.width + options.y.labelGap;
        if (!options.y.title.empty()) {
            left += options.y.titleFontSize * 1.5;
        }
    }

    double right = options.padding.right;
    if (options.hasSecondaryY && options.secondaryY.visible) {
        right += options.plotGap + secondaryMetrics.width + options.secondaryY.labelGap;
        if (!options.secondaryY.title.empty()) {
            right += options.secondaryY.titleFontSize * 1.5;
        }
    }

    const double top = options.padding.top;
    double bottom = options.padding.bottom;
    double xLabelHeight = 0.0;
    if (options.x.visible) {
        xLabelHeight = measure("0", options.x.labelFontSize).height;
        if (!(xLabelHeight > 0.0)) {
            xLabelHeight = options.x.labelFontSize * 1.25;
        }
        bottom += xLabelHeight + options.x.labelGap;
        if (!options.x.title.empty()) {
            bottom += options.x.titleFontSize * 1.5;
        }
    }

    const double plotLeft = std::clamp(left, 0.0, canvasWidth);
    const double plotTop = std::clamp(top, 0.0, canvasHeight);
    const double plotRight = std::max(plotLeft, std::clamp(canvasWidth - right, 0.0, canvasWidth));
    double plotBottom =
        std::max(plotTop, std::clamp(canvasHeight - bottom, 0.0, canvasHeight));
    layout.plot = RectD::FromLTRB(plotLeft, plotTop, plotRight, plotBottom);
    layout.valid = layout.plot.width >= options.minPlotWidth &&
                   layout.plot.height >= options.minPlotHeight;

    // ---- x 轴草稿：按可用宽度自动减少刻度 / 加大类别步长
    DraftAxis xDraft;
    if (!options.categoryLabels.empty()) {
        int stride = 1;
        const int count = static_cast<int>(options.categoryLabels.size());
        xDraft = draftCategory(options.categoryLabels, stride);
        if (options.x.visible && options.x.avoidLabelOverlap) {
            while (stride < count &&
                   !tickLabelsFit(xDraft, options.x, measure, layout.plot, canvasWidth)) {
                ++stride;
                xDraft = draftCategory(options.categoryLabels, stride);
            }
        }
    } else if (!options.x.tickLabels.empty()) {
        const std::size_t labelCount = options.x.tickLabels.size();
        const RangeD defaultDomain{0.0, static_cast<double>(labelCount > 1 ? labelCount - 1 : 1)};
        const RangeD xDomain = (bounds.hasData && !bounds.x.degenerate())
                                   ? bounds.x
                                   : defaultDomain;
        int stride = 1;
        const int count = static_cast<int>(labelCount);
        xDraft.scale = ScaleType::Linear;
        xDraft.domain = ensureSpan(xDomain);
        applyCustomTickLabels(xDraft, options.x.tickLabels, stride);
        if (options.x.visible && options.x.avoidLabelOverlap) {
            while (stride < count &&
                   !tickLabelsFit(xDraft, options.x, measure, layout.plot, canvasWidth)) {
                ++stride;
                applyCustomTickLabels(xDraft, options.x.tickLabels, stride);
            }
        }
    } else {
        const RangeD xData = bounds.hasData ? bounds.x : RangeD{0.0, 1.0};
        auto buildX = [&](int target) {
            return options.x.scale == ScaleType::Log
                       ? draftLog(xData, options.x, target)
                       : draftLinear(xData, options.x, target);
        };

        const int count = std::max(2, options.x.targetTickCount);
        xDraft = buildX(count);
        if (options.x.visible && options.x.avoidLabelOverlap) {
            for (int candidateCount = count; candidateCount >= 2; --candidateCount) {
                DraftAxis candidate = buildX(candidateCount);
                const bool fits =
                    tickLabelsFit(candidate, options.x, measure, layout.plot, canvasWidth);
                if (fits || candidateCount == 2) {
                    xDraft = std::move(candidate);
                    break;
                }
            }
        }
    }

    // 若 X 轴刻度开启了竖排或偏移旋转角度，标签高度可能高于单行数字，据此向上收紧 plotBottom。
    if (options.x.visible &&
        (options.x.labelDirection == TextDirection::Vertical ||
         std::abs(options.x.labelAngle) > 1e-6)) {
        const double measuredHeight = measureTicks(xDraft, options.x, measure).height;
        if (measuredHeight > xLabelHeight) {
            bottom += measuredHeight - xLabelHeight;
            plotBottom = std::max(plotTop, std::clamp(canvasHeight - bottom, 0.0, canvasHeight));
            layout.plot = RectD::FromLTRB(plotLeft, plotTop, plotRight, plotBottom);
            layout.valid = layout.plot.width >= options.minPlotWidth &&
                           layout.plot.height >= options.minPlotHeight;
        }
    }

    // ---- 最终坐标轴
    const RangeD xPixelRange{layout.plot.left(), layout.plot.right()};
    const RangeD yPixelRange =
        options.yDirection == YDirection::Up
            ? RangeD{layout.plot.bottom(), layout.plot.top()}
            : RangeD{layout.plot.top(), layout.plot.bottom()};

    layout.xAxis = finalizeAxis(xDraft, options.x, AxisPosition::Bottom, xPixelRange, measure);
    layout.yAxis = finalizeAxis(yDraft, options.y, AxisPosition::Left, yPixelRange, measure);
    if (options.hasSecondaryY) {
        layout.secondaryYAxis = finalizeAxis(secondaryDraft, options.secondaryY,
                                             AxisPosition::Right, yPixelRange, measure);
    }

    return layout;
}

// ---------------------------------------------------------------- 坐标转换

Vec2d ChartLayout::toPixel(double x, double y) const noexcept {
    return {xAxis.map(x), yAxis.map(y)};
}

Vec2d ChartLayout::toPixel(const Vec2d& dataPoint) const noexcept {
    return toPixel(dataPoint.x, dataPoint.y);
}

Vec2d ChartLayout::toPixelSecondaryY(double x, double y) const noexcept {
    return {xAxis.map(x), secondaryYAxis.map(y)};
}

Vec2d ChartLayout::fromPixel(const Vec2d& pixel) const noexcept {
    return {xAxis.invert(pixel.x), yAxis.invert(pixel.y)};
}

double ChartLayout::toPixelX(double x) const noexcept {
    return xAxis.map(x);
}

double ChartLayout::toPixelY(double y) const noexcept {
    return yAxis.map(y);
}

double ChartLayout::fromPixelX(double pixel) const noexcept {
    return xAxis.invert(pixel);
}

double ChartLayout::fromPixelY(double pixel) const noexcept {
    return yAxis.invert(pixel);
}

// ---------------------------------------------------------------- 柱形几何

std::vector<BarRect> computeBarRects(const Dataset& dataset, const ChartLayout& layout,
                                     BarMode mode, const BarOptions& options) {
    std::vector<BarRect> rects;
    if (!layout.valid || dataset.series.empty() || mode == BarMode::None) {
        return rects;
    }

    const std::size_t categoryCount = dataset.categoryCount();
    const std::size_t seriesCount = dataset.series.size();
    if (categoryCount == 0 || seriesCount == 0) {
        return rects;
    }

    const double band = layout.plot.width / static_cast<double>(categoryCount);
    const double groupWidth = band * (1.0 - math::clamp(options.groupPaddingRatio, 0.0, 0.9));
    if (!(band > 0.0)) {
        return rects;
    }

    // 纵向：把值映射到像素并夹在绘图区内，返回 [top, bottom]。
    auto vertical = [&](double fromValue, double toValue) {
        const double a = layout.toPixelY(fromValue);
        const double b = layout.toPixelY(toValue);
        return std::pair{math::clamp(std::min(a, b), layout.plot.top(), layout.plot.bottom()),
                         math::clamp(std::max(a, b), layout.plot.top(), layout.plot.bottom())};
    };

    if (mode == BarMode::Grouped) {
        const double rawBarWidth =
            groupWidth / static_cast<double>(seriesCount) * (1.0 - math::clamp(options.barGapRatio, 0.0, 0.9));
        const double barWidth = std::min(rawBarWidth, options.maxBarWidth);
        const double slot = groupWidth / static_cast<double>(seriesCount);

        for (std::size_t s = 0; s < seriesCount; ++s) {
            const Series& series = dataset.series[s];
            const std::size_t count = std::min(series.points.size(), categoryCount);
            for (std::size_t i = 0; i < count; ++i) {
                const DataPoint& point = series.points[i];
                if (!point.valid()) {
                    continue;
                }

                const double categoryCenter = layout.toPixelX(static_cast<double>(i));
                const double center =
                    categoryCenter - groupWidth * 0.5 + (static_cast<double>(s) + 0.5) * slot;

                const auto [top, bottom] = vertical(0.0, point.y);
                RectD rect{center - barWidth * 0.5, top, barWidth, bottom - top};
                if (rect.empty()) {
                    continue;
                }
                rects.push_back({s, i, rect, point.y, 0.0});
            }
        }
        return rects;
    }

    // 堆叠：正负值分别累计，避免正负柱互相吃掉。
    std::vector<double> positive(categoryCount, 0.0);
    std::vector<double> negative(categoryCount, 0.0);
    for (std::size_t s = 0; s < seriesCount; ++s) {
        const Series& series = dataset.series[s];
        const std::size_t count = std::min(series.points.size(), categoryCount);
        for (std::size_t i = 0; i < count; ++i) {
            const DataPoint& point = series.points[i];
            if (!point.valid()) {
                continue;
            }

            const double base = point.y >= 0.0 ? positive[i] : negative[i];
            const double end = base + point.y;
            (point.y >= 0.0 ? positive[i] : negative[i]) = end;

            const double categoryCenter = layout.toPixelX(static_cast<double>(i));
            const auto [top, bottom] = vertical(base, end);
            RectD rect{categoryCenter - groupWidth * 0.5, top, groupWidth, bottom - top};
            if (rect.empty()) {
                continue;
            }
            rects.push_back({s, i, rect, point.y, base});
        }
    }
    return rects;
}

// ---------------------------------------------------------------- 饼图布局

PieLayout computePieLayout(Vec2d canvasSize, const PieChartData& data,
                           const PieLayoutOptions& options) {
    PieLayout layout;
    const double canvasWidth = std::max(0.0, canvasSize.x);
    const double canvasHeight = std::max(0.0, canvasSize.y);
    layout.canvas = RectD{0.0, 0.0, canvasWidth, canvasHeight};

    const double areaLeft = std::min(std::max(0.0, options.padding.left), canvasWidth);
    const double areaTop = std::min(std::max(0.0, options.padding.top), canvasHeight);
    const double areaRight =
        std::max(areaLeft, canvasWidth - std::max(0.0, options.padding.right));
    const double areaBottom =
        std::max(areaTop, canvasHeight - std::max(0.0, options.padding.bottom));
    const RectD area = RectD::FromLTRB(areaLeft, areaTop, areaRight, areaBottom);

    if (area.empty()) {
        return layout;
    }

    layout.center = area.center();
    const double radiusRatio = math::clamp(options.radiusRatio, 0.05, 1.0);
    layout.outerRadius =
        std::min(area.width, area.height) * 0.5 * radiusRatio;
    layout.innerRadius =
        layout.outerRadius * math::clamp(options.innerRadiusRatio, 0.0, 0.95);

    const double total = data.total();
    if (!(total > 0.0) || !(layout.outerRadius > 0.0)) {
        return layout;
    }

    // 最后一个可用扇区强制收口到 2pi，避免累加误差留下一条缝。
    std::size_t lastUsable = kInvalidIndex;
    for (std::size_t i = 0; i < data.slices.size(); ++i) {
        const double value = data.slices[i].value;
        if (std::isfinite(value) && value > 0.0) {
            lastUsable = i;
        }
    }

    layout.valid = true;
    double angle = options.startAngle;
    for (std::size_t i = 0; i < data.slices.size(); ++i) {
        const double value = data.slices[i].value;
        if (!std::isfinite(value) || !(value > 0.0)) {
            continue;
        }

        PieSector sector;
        sector.sliceIndex = i;
        sector.value = value;
        sector.fraction = value / total;
        sector.startAngle = angle;
        sector.sweepAngle = i == lastUsable
                                ? math::kTwoPi - (angle - options.startAngle)
                                : sector.fraction * math::kTwoPi;
        sector.midAngle = angle + sector.sweepAngle * 0.5;
        sector.labelAnchor = {
            layout.center.x + std::cos(sector.midAngle) * (layout.outerRadius + options.labelOffset),
            layout.center.y + std::sin(sector.midAngle) * (layout.outerRadius + options.labelOffset)};
        angle += sector.sweepAngle;

        layout.sectors.push_back(sector);
    }

    return layout;
}

std::vector<Vec2d> sectorPolygon(const PieSector& sector, const PieLayout& layout,
                                 int fullCircleSegments) {
    const double sweep = sector.sweepAngle;
    const int full = std::clamp(fullCircleSegments, 12, 512);
    int segments = static_cast<int>(std::ceil(std::abs(sweep) / math::kTwoPi * full));
    segments = std::clamp(segments, 2, 512);
    const double step = sweep / static_cast<double>(segments);
    const bool annulus = layout.innerRadius > 0.0;

    std::vector<Vec2d> points;
    points.reserve(static_cast<std::size_t>(segments) * (annulus ? 2u : 1u) + 2u);
    if (!annulus) {
        points.push_back(layout.center);
    }

    auto at = [&](double angle, double radius) {
        return Vec2d{layout.center.x + std::cos(angle) * radius,
                     layout.center.y + std::sin(angle) * radius};
    };

    for (int i = 0; i <= segments; ++i) {
        points.push_back(at(sector.startAngle + step * static_cast<double>(i),
                            layout.outerRadius));
    }
    if (annulus) {
        for (int i = segments; i >= 0; --i) {
            points.push_back(at(sector.startAngle + step * static_cast<double>(i),
                                layout.innerRadius));
        }
    }
    return points;
}

}  // namespace kiriyama::core
