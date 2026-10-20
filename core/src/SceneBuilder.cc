#include "kiriyama/core/SceneBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <utility>

#include "kiriyama/math/Curve.h"
#include "kiriyama/math/MathUtils.h"

namespace kiriyama::core {
namespace {

using math::kHalfPi;
using math::kTwoPi;

/// 网格线贴到绘图区边缘 0.5px 内就不再画，否则和轴线重叠成一条粗线。
constexpr double kEdgeTolerance = 0.5;

// ---------------------------------------------------------------- 上下文
//
// 之前 canvasSize / LayoutOptions / SceneBuildOptions 在每个 build* 之间
// 长途透传，签名越叠越长。这里收拢成一个只读上下文：
//   ctx.canvas  画布尺寸（几何的原点基准）
//   ctx.lo      排版选项（字号 / gap / 测量回调）
//   ctx.opt     本图表的绘制选项（哪些要画、几何参数、度量）
// 主题不在这里——描述树只发语义 token，颜色解析在渲染层。

struct BuildContext {
    Vec2d canvas{};
    const LayoutOptions& lo;
    const SceneBuildOptions& opt;
};

// ---------------------------------------------------------------- 节点工厂
//
// 颜色一律 Paint（语义 token 或调用方给的直接色），这里没有任何
// "从 theme 取色"的代码——主题切换不重建场景树的前提。

std::unique_ptr<SceneNode> makeNode(NodeType type, std::string key, std::string name = {}) {
    auto node = std::make_unique<SceneNode>(type, std::move(key));
    if (!name.empty()) {
        node->setName(std::move(name));
    }
    return node;
}

std::unique_ptr<SceneNode> makeGroup(std::string key, std::string name = {}) {
    return makeNode(NodeType::Group, std::move(key), std::move(name));
}

std::unique_ptr<SceneNode> makeRect(std::string key, const RectD& frame, Paint fill,
                                    double cornerRadius = 0.0, std::string name = {}) {
    auto node = makeNode(NodeType::Rect, std::move(key), std::move(name));
    node->setFrame(frame);
    SceneStyle style;
    style.fill = std::move(fill);
    style.cornerRadius = cornerRadius;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makeLine(std::string key, Vec2d from, Vec2d to, Paint stroke,
                                    double width) {
    auto node = makeNode(NodeType::Line, std::move(key));
    node->setPoints({from, to});
    SceneStyle style;
    style.stroke = std::move(stroke);
    style.strokeWidth = width;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makePolyline(std::string key, std::vector<Vec2d> points,
                                        Paint stroke, double width) {
    auto node = makeNode(NodeType::Polyline, std::move(key));
    node->setPoints(std::move(points));
    SceneStyle style;
    style.stroke = std::move(stroke);
    style.strokeWidth = width;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makePolygon(std::string key, std::vector<Vec2d> points, Paint fill,
                                       Paint stroke, double strokeWidth) {
    auto node = makeNode(NodeType::Polygon, std::move(key));
    node->setPoints(std::move(points));
    SceneStyle style;
    style.fill = std::move(fill);
    style.stroke = std::move(stroke);
    style.strokeWidth = strokeWidth;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makeEllipse(std::string key, Vec2d center, double radius,
                                       Paint fill) {
    auto node = makeNode(NodeType::Ellipse, std::move(key));
    node->setFrame({center.x - radius, center.y - radius, radius * 2.0, radius * 2.0});
    SceneStyle style;
    style.fill = std::move(fill);
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makeText(std::string key, std::string text, const RectD& frame,
                                    double fontSize, Paint color, TextAlign align,
                                    TextBaseline baseline) {
    auto node = makeNode(NodeType::Text, std::move(key));
    node->setText(std::move(text));
    node->setFrame(frame);
    SceneStyle style;
    style.fill = std::move(color);
    style.fontSize = fontSize;
    style.textAlign = align;
    style.textBaseline = baseline;
    node->setStyle(style);
    return node;
}

// ---------------------------------------------------------------- 文本辅助

TextMetrics measureText(const LayoutOptions& layoutOptions, std::string_view text,
                        double fontSize) {
    return measureText(layoutOptions.measureText, text, fontSize);
}

std::string formatValueLabel(double value, double span) {
    TickFormat format;
    format.maxDecimals = 2;
    double step = 1.0;
    if (std::abs(value - std::round(value)) > 1e-9) {
        step = std::max(span / 1000.0, 1e-9);
    }
    return format.format(value, step);
}

// ---------------------------------------------------------------- 组装

std::unique_ptr<SceneNode> buildGrid(const ChartLayout& layout, const BuildContext& ctx,
                                     bool categoryAxis) {
    auto group = makeGroup("grid", "grid");
    if (!ctx.opt.showGrid) {
        return group;
    }

    int index = 0;
    if (layout.xAxis.visible && (!categoryAxis || ctx.opt.showCategoryGrid)) {
        for (const TickMark& tick : layout.xAxis.ticks) {
            const double x = tick.position;
            if (std::abs(x - layout.plot.left()) < kEdgeTolerance ||
                std::abs(x - layout.plot.right()) < kEdgeTolerance) {
                continue;
            }
            group->appendChild(makeLine("grid.x." + std::to_string(index++),
                                        {x, layout.plot.top()}, {x, layout.plot.bottom()},
                                        ColorToken::Grid, ctx.opt.gridWidth));
        }
    }

    index = 0;
    if (layout.yAxis.visible) {
        for (const TickMark& tick : layout.yAxis.ticks) {
            const double y = tick.position;
            if (std::abs(y - layout.plot.top()) < kEdgeTolerance ||
                std::abs(y - layout.plot.bottom()) < kEdgeTolerance) {
                continue;
            }
            group->appendChild(makeLine("grid.y." + std::to_string(index++),
                                        {layout.plot.left(), y}, {layout.plot.right(), y},
                                        ColorToken::Grid, ctx.opt.gridWidth));
        }
    }
    return group;
}

std::unique_ptr<SceneNode> buildAxisTexts(const ChartLayout& layout, const BuildContext& ctx) {
    auto group = makeGroup("axis.labels", "axis labels");
    if (!ctx.opt.showAxisLabels) {
        return group;
    }

    if (layout.xAxis.visible) {
        const double fontSize = ctx.lo.x.labelFontSize;
        const double gap = ctx.lo.x.labelGap;
        for (std::size_t i = 0; i < layout.xAxis.ticks.size(); ++i) {
            const TickMark& tick = layout.xAxis.ticks[i];
            if (tick.label.empty()) {
                continue;
            }
            const TextMetrics metrics = measureText(ctx.lo, tick.label, fontSize);
            const RectD frame = alignTextFrame({tick.position, layout.plot.bottom() + gap}, metrics,
                                        TextAlign::Center, TextBaseline::Top);
            group->appendChild(makeText("axis.x.tick." + std::to_string(i), tick.label, frame,
                                        fontSize, ColorToken::Label, TextAlign::Center,
                                        TextBaseline::Top));
        }
    }

    if (layout.yAxis.visible) {
        const double fontSize = ctx.lo.y.labelFontSize;
        const double gap = ctx.lo.y.labelGap;
        for (std::size_t i = 0; i < layout.yAxis.ticks.size(); ++i) {
            const TickMark& tick = layout.yAxis.ticks[i];
            if (tick.label.empty()) {
                continue;
            }
            const TextMetrics metrics = measureText(ctx.lo, tick.label, fontSize);
            const RectD frame = alignTextFrame({layout.plot.left() - gap, tick.position}, metrics,
                                        TextAlign::Right, TextBaseline::Middle);
            group->appendChild(makeText("axis.y.tick." + std::to_string(i), tick.label, frame,
                                        fontSize, ColorToken::Label, TextAlign::Right,
                                        TextBaseline::Middle));
        }
    }

    if (layout.hasSecondaryY && layout.secondaryYAxis.visible) {
        const double fontSize = ctx.lo.secondaryY.labelFontSize;
        const double gap = ctx.lo.plotGap;
        for (std::size_t i = 0; i < layout.secondaryYAxis.ticks.size(); ++i) {
            const TickMark& tick = layout.secondaryYAxis.ticks[i];
            if (tick.label.empty()) {
                continue;
            }
            const TextMetrics metrics = measureText(ctx.lo, tick.label, fontSize);
            const RectD frame = alignTextFrame({layout.plot.right() + gap, tick.position}, metrics,
                                        TextAlign::Left, TextBaseline::Middle);
            group->appendChild(makeText("axis.y2.tick." + std::to_string(i), tick.label, frame,
                                        fontSize, ColorToken::Label, TextAlign::Left,
                                        TextBaseline::Middle));
        }
    }
    return group;
}

std::unique_ptr<SceneNode> buildAxisTitles(const ChartLayout& layout, const BuildContext& ctx) {
    auto group = makeGroup("axis.titles", "axis titles");
    if (!ctx.opt.showAxisTitles) {
        return group;
    }

    if (layout.xAxis.visible && !ctx.lo.x.title.empty()) {
        const double fontSize = ctx.lo.x.titleFontSize;
        const TextMetrics metrics = measureText(ctx.lo, ctx.lo.x.title, fontSize);
        const double y = std::max(layout.plot.bottom() + fontSize * 0.75,
                                  ctx.canvas.y - ctx.lo.padding.bottom - fontSize * 0.75);
        const RectD frame = alignTextFrame({layout.plot.center().x, y}, metrics, TextAlign::Center,
                                    TextBaseline::Middle);
        group->appendChild(makeText("axis.x.title", ctx.lo.x.title, frame, fontSize,
                                    ColorToken::Label, TextAlign::Center,
                                    TextBaseline::Middle));
    }

    if (layout.yAxis.visible && !ctx.lo.y.title.empty()) {
        const double fontSize = ctx.lo.y.titleFontSize;
        const TextMetrics metrics = measureText(ctx.lo, ctx.lo.y.title, fontSize);
        // 旋转 -90°：frame 放在原点，再把盒子中心搬到目标位置。
        const Vec2d center{ctx.lo.padding.left + fontSize * 0.75, layout.plot.center().y};
        auto node = makeText("axis.y.title", ctx.lo.y.title,
                             {0.0, 0.0, metrics.width, metrics.height}, fontSize,
                             ColorToken::Label, TextAlign::Center, TextBaseline::Middle);
        node->setTransform(Affine2D::Translation(center) * Affine2D::Rotation(-kHalfPi) *
                           Affine2D::Translation({-metrics.width * 0.5, -metrics.height * 0.5}));
        group->appendChild(std::move(node));
    }

    if (layout.hasSecondaryY && ctx.lo.secondaryY.visible && !ctx.lo.secondaryY.title.empty()) {
        const double fontSize = ctx.lo.secondaryY.titleFontSize;
        const TextMetrics metrics = measureText(ctx.lo, ctx.lo.secondaryY.title, fontSize);
        const Vec2d center{ctx.canvas.x - ctx.lo.padding.right - fontSize * 0.75,
                           layout.plot.center().y};
        auto node = makeText("axis.y2.title", ctx.lo.secondaryY.title,
                             {0.0, 0.0, metrics.width, metrics.height}, fontSize,
                             ColorToken::Label, TextAlign::Center, TextBaseline::Middle);
        node->setTransform(Affine2D::Translation(center) * Affine2D::Rotation(kHalfPi) *
                           Affine2D::Translation({-metrics.width * 0.5, -metrics.height * 0.5}));
        group->appendChild(std::move(node));
    }
    return group;
}

std::unique_ptr<SceneNode> buildAxisLines(const ChartLayout& layout, const BuildContext& ctx) {
    auto group = makeGroup("axis.lines", "axis lines");
    if (!ctx.opt.showAxisLines) {
        return group;
    }
    if (layout.xAxis.visible) {
        group->appendChild(makeLine("axis.x.line", {layout.plot.left(), layout.plot.bottom()},
                                    {layout.plot.right(), layout.plot.bottom()},
                                    ColorToken::AxisLine, ctx.opt.axisWidth));
    }
    if (layout.yAxis.visible) {
        group->appendChild(makeLine("axis.y.line", {layout.plot.left(), layout.plot.top()},
                                    {layout.plot.left(), layout.plot.bottom()},
                                    ColorToken::AxisLine, ctx.opt.axisWidth));
    }
    if (layout.hasSecondaryY && layout.secondaryYAxis.visible) {
        group->appendChild(makeLine("axis.y2.line", {layout.plot.right(), layout.plot.top()},
                                    {layout.plot.right(), layout.plot.bottom()},
                                    ColorToken::AxisLine, ctx.opt.axisWidth));
    }
    return group;
}

/// 把一条系列拆成"连续有效点"的若干段，并转成像素坐标。
std::vector<std::vector<Vec2d>> splitSeries(const Series& series, const ChartLayout& layout,
                                            bool useSecondaryY) {
    std::vector<std::vector<Vec2d>> segments;
    std::vector<Vec2d> current;
    auto flush = [&]() {
        if (!current.empty()) {
            segments.push_back(std::move(current));
            current.clear();
        }
    };

    for (const DataPoint& point : series.points) {
        if (!point.valid()) {
            flush();
            continue;
        }
        current.push_back(useSecondaryY ? layout.toPixelSecondaryY(point.x, point.y)
                                        : layout.toPixel(point.x, point.y));
    }
    flush();
    return segments;
}

std::unique_ptr<SceneNode> buildLineSeries(const Dataset& dataset, const ChartLayout& layout,
                                           const BuildContext& ctx) {
    auto group = makeGroup("series", "series");
    const SceneBuildOptions& options = ctx.opt;
    const std::vector<std::string> keys = dataset.seriesKeys();

    // 右轴系列从后往前数，和 boundsOfSeries 保持一致。
    const std::size_t secondaryFirst =
        (options.secondarySeriesCount > 0 && options.secondarySeriesCount < dataset.series.size())
            ? dataset.series.size() - options.secondarySeriesCount
            : dataset.series.size();

    const double areaBaseline = [&]() {
        if (!layout.yAxis.domain.contains(0.0) || !layout.valid) {
            return layout.plot.bottom();
        }
        return math::clamp(layout.toPixelY(0.0), layout.plot.top(), layout.plot.bottom());
    }();

    for (std::size_t s = 0; s < dataset.series.size(); ++s) {
        const Series& series = dataset.series[s];
        const bool useSecondaryY = s >= secondaryFirst;

        auto seriesNode = makeGroup(keys[s], series.name.empty() ? "series" : series.name);
        const auto segments = splitSeries(series, layout, useSecondaryY);

        for (std::size_t k = 0; k < segments.size(); ++k) {
            const std::vector<Vec2d>& raw = segments[k];
            if (raw.empty()) {
                continue;
            }

            const std::string segmentKey = keys[s] + ".seg." + std::to_string(k);
            const std::vector<Vec2d> curve =
                options.curve == LineCurve::Monotone
                    ? math::monotoneCubicSpline(raw, options.curveSegments)
                    : raw;

            if (options.showArea && curve.size() >= 2) {
                std::vector<Vec2d> area = curve;
                area.push_back({curve.back().x, areaBaseline});
                area.push_back({curve.front().x, areaBaseline});
                // 系列色的淡版：token 不变，只缩透明度——主题切换后面积色跟着变。
                seriesNode->appendChild(makePolygon(segmentKey + ".area", std::move(area),
                                                    Paint::series(
                                                        s,
                                                        static_cast<float>(
                                                            math::saturate(options.areaOpacity))),
                                                    Paint{}, 0.0));
            }

            if (options.lineWidth > 0.0 && curve.size() >= 2) {
                seriesNode->appendChild(
                    makePolyline(segmentKey + ".line", curve, Paint::series(s),
                                 options.lineWidth));
            }

            // 孤立点：折线只有两个端点都画不出来，给它一个点。
            if (curve.size() == 1 && !options.showPoints && options.pointRadius > 0.0) {
                seriesNode->appendChild(makeEllipse(segmentKey + ".dot", curve.front(),
                                                     std::max(options.pointRadius, 1.5),
                                                     Paint::series(s)));
            }
        }

        if (options.showPoints && options.pointRadius > 0.0) {
            auto pointsGroup = makeGroup(keys[s] + ".points", "points");
            for (std::size_t j = 0; j < series.points.size(); ++j) {
                const DataPoint& point = series.points[j];
                if (!point.valid()) {
                    continue;
                }
                const Vec2d pixel = useSecondaryY ? layout.toPixelSecondaryY(point.x, point.y)
                                                  : layout.toPixel(point.x, point.y);
                pointsGroup->appendChild(makeEllipse(keys[s] + ".p." + std::to_string(j), pixel,
                                                     options.pointRadius, Paint::series(s)));
            }
            seriesNode->appendChild(std::move(pointsGroup));
        }

        if (seriesNode->childCount() > 0) {
            group->appendChild(std::move(seriesNode));
        }
    }
    return group;
}

std::unique_ptr<SceneNode> buildBars(const std::vector<BarRect>& bars, const ChartLayout& layout,
                                     const BuildContext& ctx) {
    auto group = makeGroup("bars", "bars");
    const SceneBuildOptions& options = ctx.opt;

    for (const BarRect& bar : bars) {
        const std::string key = "bar." + std::to_string(bar.seriesIndex) + "." +
                                std::to_string(bar.categoryIndex);
        group->appendChild(makeRect(key, bar.rect,
                                    Paint::series(bar.seriesIndex,
                                                  static_cast<float>(
                                                      math::saturate(options.barOpacity))),
                                    options.barCornerRadius, "bar"));
    }

    if (!options.showValueLabels) {
        return group;
    }

    // 数值标签单独一组，叠在柱子上方，避免和柱子一起被裁剪。
    auto labels = makeGroup("bar.labels", "bar value labels");
    const double fontSize = options.labelFontSize;
    const double dataSpan = std::max(1e-9, layout.yAxis.domain.span());
    for (const BarRect& bar : bars) {
        const std::string text = formatValueLabel(bar.value, dataSpan);
        const TextMetrics metrics = measureText(ctx.lo, text, fontSize);
        if (metrics.width <= 0.0 || bar.rect.width < 6.0 ||
            bar.rect.height < metrics.height + 6.0) {
            continue;
        }
        const bool positive = bar.value >= 0.0;
        const Vec2d anchor{bar.rect.center().x,
                           positive ? bar.rect.top() - 4.0 : bar.rect.bottom() + 4.0};
        const TextBaseline baseline = positive ? TextBaseline::Bottom : TextBaseline::Top;
        const RectD frame = alignTextFrame(anchor, metrics, TextAlign::Center, baseline);
        labels->appendChild(makeText("bar.value." + std::to_string(bar.seriesIndex) + "." +
                                         std::to_string(bar.categoryIndex),
                                     text, frame, fontSize, ColorToken::Label,
                                     TextAlign::Center, baseline));
    }
    group->appendChild(std::move(labels));
    return group;
}

std::unique_ptr<SceneNode> buildChartTitle(const BuildContext& ctx, Vec2d topLeft) {
    const TextMetrics metrics = measureText(ctx.lo, ctx.opt.title, ctx.opt.titleFontSize);
    const RectD frame = alignTextFrame(topLeft, metrics, TextAlign::Left, TextBaseline::Top);
    return makeText("title", ctx.opt.title, frame, ctx.opt.titleFontSize, ColorToken::Title,
                    TextAlign::Left, TextBaseline::Top);
}

std::unique_ptr<SceneNode> buildEmptyText(const BuildContext& ctx) {
    auto group = makeGroup("empty", "empty");
    if (!ctx.opt.showEmptyText || ctx.opt.emptyText.empty()) {
        return group;
    }
    const TextMetrics metrics = measureText(ctx.lo, ctx.opt.emptyText, ctx.opt.labelFontSize);
    const Vec2d center{ctx.canvas.x * 0.5, ctx.canvas.y * 0.5};
    const RectD frame = alignTextFrame(center, metrics, TextAlign::Center, TextBaseline::Middle);
    group->appendChild(makeText("empty.text", ctx.opt.emptyText, frame, ctx.opt.labelFontSize,
                                ColorToken::EmptyText, TextAlign::Center,
                                TextBaseline::Middle));
    return group;
}

}  // namespace

// ---------------------------------------------------------------- 折线 / 柱状

namespace {

SceneBuildResult buildCartesian(const Dataset& dataset, Vec2d canvasSize,
                                const LayoutOptions& layoutOptions,
                                const SceneBuildOptions& options, ChartKind kind) {
    SceneBuildResult result;
    result.issues = dataset.inspectIssues();

    canvasSize = {std::max(0.0, canvasSize.x), std::max(0.0, canvasSize.y)};

    LayoutOptions lo = layoutOptions;
    if (kind == ChartKind::Bar && lo.categoryLabels.empty()) {
        lo.categoryLabels = dataset.categories;
    }

    const std::size_t secondaryCount =
        std::min(options.secondarySeriesCount, dataset.series.size());
    const bool wantsSecondary = secondaryCount > 0 && secondaryCount < dataset.series.size();
    if (wantsSecondary) {
        lo.hasSecondaryY = true;
    }

    const double titleTop = lo.padding.top;
    if (options.showChartTitle && !options.title.empty()) {
        const double titleHeight = measureText(lo, options.title, options.titleFontSize).height;
        lo.padding.top += titleHeight + options.titleGap;
    }

    const BarMode barMode = kind == ChartKind::Bar ? options.barMode : BarMode::None;
    const DataBounds bounds = computeBounds(dataset, barMode);
    const DataBounds secondaryBounds =
        wantsSecondary ? computeSeriesBounds(dataset, dataset.series.size() - secondaryCount,
                                             secondaryCount)
                       : DataBounds{};

    result.layout = wantsSecondary ? computeLayout(canvasSize, bounds, secondaryBounds, lo)
                                   : computeLayout(canvasSize, bounds, lo);
    const ChartLayout& layout = result.layout;

    const BuildContext ctx{canvasSize, lo, options};

    auto root = makeGroup("chart", "chart");
    root->appendChild(makeRect("background", {0.0, 0.0, canvasSize.x, canvasSize.y},
                               ColorToken::Background));

    const bool hasData = dataset.validCount() > 0;
    if (!hasData || !layout.valid) {
        if (!hasData) {
            root->appendChild(buildEmptyText(ctx));
        }
        result.root = std::move(root);
        result.valid = false;
        return result;
    }

    if (options.showPlotBackground) {
        root->appendChild(makeRect("plot.bg", layout.plot, ColorToken::PlotBackground));
    }
    root->appendChild(buildGrid(layout, ctx, kind == ChartKind::Bar));

    if (kind == ChartKind::Bar) {
        result.bars = computeBarRects(dataset, layout, options.barMode, options.barOptions);
        root->appendChild(buildBars(result.bars, layout, ctx));
    } else {
        root->appendChild(buildLineSeries(dataset, layout, ctx));
    }

    root->appendChild(buildAxisLines(layout, ctx));
    root->appendChild(buildAxisTexts(layout, ctx));
    root->appendChild(buildAxisTitles(layout, ctx));

    if (options.showChartTitle && !options.title.empty()) {
        root->appendChild(buildChartTitle(ctx, {lo.padding.left, titleTop}));
    }

    result.root = std::move(root);
    result.valid = true;
    return result;
}

}  // namespace

SceneBuildResult buildLineChart(const Dataset& dataset, Vec2d canvasSize,
                                const LayoutOptions& layoutOptions,
                                const SceneBuildOptions& options) {
    return buildCartesian(dataset, canvasSize, layoutOptions, options, ChartKind::Line);
}

SceneBuildResult buildBarChart(const Dataset& dataset, Vec2d canvasSize,
                               const LayoutOptions& layoutOptions,
                               const SceneBuildOptions& options) {
    return buildCartesian(dataset, canvasSize, layoutOptions, options, ChartKind::Bar);
}

// ---------------------------------------------------------------- 饼图

PieSlicesBuildResult buildPieSlices(const PieChartData& data, Vec2d canvasSize,
                                    const PieStyle& style,
                                    std::shared_ptr<const std::vector<Color>> seriesPalette) {
    PieSlicesBuildResult result;
    canvasSize = {std::max(0.0, canvasSize.x), std::max(0.0, canvasSize.y)};
    result.layout = computePieLayout(canvasSize, data, style.layout);
    const PieLayout& layout = result.layout;
    result.valid = layout.valid && !data.slices.empty();

    auto sectors = makeGroup("slices", "slices");
    sectors->mutableStyle().seriesPalette = std::move(seriesPalette);
    if (!result.valid) {
        result.root = std::move(sectors);
        return result;
    }

    LayoutOptions measureOptions;
    measureOptions.measureText = style.textMeasure;

    const Paint gapPaint =
        style.gapColor.visible() ? Paint{style.gapColor} : Paint{ColorToken::Background};
    const Paint labelPaint =
        style.labelColor.visible() ? Paint{style.labelColor} : Paint{ColorToken::Label};

    for (const PieSector& sector : layout.sectors) {
        const std::size_t index = sector.sliceIndex;
        std::vector<Vec2d> points = sectorPolygon(sector, layout, style.segments);
        sectors->appendChild(makePolygon("slice." + std::to_string(index), std::move(points),
                                         Paint::series(index), gapPaint,
                                         std::max(0.0, style.gapWidth)));

        if (!style.showLabels || index >= data.slices.size()) {
            continue;
        }
        const PieSlice& slice = data.slices[index];
        std::string text = slice.label;
        if (style.showValues) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), " %.1f%%", sector.fraction * 100.0);
            if (!text.empty()) {
                text += " ";
            }
            text += buffer;
        }
        if (text.empty()) {
            continue;
        }

        const TextMetrics metrics = measureText(measureOptions, text, style.labelFontSize);
        const double cosine = std::cos(sector.midAngle);
        TextAlign align = TextAlign::Center;
        double offset = 0.0;
        if (cosine > 0.2) {
            align = TextAlign::Left;
            offset = 4.0;
        } else if (cosine < -0.2) {
            align = TextAlign::Right;
            offset = -4.0;
        }
        const Vec2d anchor{sector.labelAnchor.x + offset, sector.labelAnchor.y};
        const RectD frame =
            alignTextFrame(anchor, metrics, align, TextBaseline::Middle)
                .confinedTo(layout.canvas, 2.0);
        sectors->appendChild(makeText("slice.label." + std::to_string(index), text, frame,
                                      style.labelFontSize, labelPaint, align,
                                      TextBaseline::Middle));
    }

    result.root = std::move(sectors);
    return result;
}

void updatePieSliceExplode(SceneNode& slicesRoot, const PieLayout& layout,
                           std::size_t sectorIndex, double radiusDelta, int segments) {
    if (sectorIndex >= layout.sectors.size()) {
        return;
    }
    const PieSector& sector = layout.sectors[sectorIndex];
    SceneNode* sliceNode =
        slicesRoot.findChildByKey("slice." + std::to_string(sector.sliceIndex));
    if (sliceNode == nullptr) {
        return;
    }
    PieLayout expanded = layout;
    expanded.outerRadius += radiusDelta;
    sliceNode->setPoints(sectorPolygon(sector, expanded, segments));
}

SceneBuildResult buildPieChart(const PieChartData& data, Vec2d canvasSize,
                               const SceneBuildOptions& options) {
    SceneBuildResult result;
    result.issues = data.inspectIssues();

    canvasSize = {std::max(0.0, canvasSize.x), std::max(0.0, canvasSize.y)};

    LayoutOptions measureOptions;
    measureOptions.measureText = options.textMeasure;

    PieStyle pieStyle;
    pieStyle.layout = options.pie;
    if (options.showChartTitle && !options.title.empty()) {
        pieStyle.layout.padding.top +=
            measureText(measureOptions, options.title, options.titleFontSize).height +
            options.titleGap;
    }
    pieStyle.showLabels = options.showPieLabels;
    pieStyle.showValues = options.showPieValues;
    pieStyle.labelFontSize = options.labelFontSize;
    pieStyle.labelColor = options.pieLabelColorOverride;
    pieStyle.gapWidth = options.pieSliceGap;
    pieStyle.gapColor = options.pieGapColor;
    pieStyle.segments = options.pieSegments;
    pieStyle.textMeasure = options.textMeasure;

    PieSlicesBuildResult builtSlices = buildPieSlices(data, canvasSize, pieStyle);
    result.pie = builtSlices.layout;

    const BuildContext ctx{canvasSize, measureOptions, options};

    auto root = makeGroup("chart", "chart");
    root->appendChild(makeRect("background", {0.0, 0.0, canvasSize.x, canvasSize.y},
                               ColorToken::Background));

    if (!result.pie.valid) {
        root->appendChild(buildEmptyText(ctx));
        result.root = std::move(root);
        result.valid = false;
        return result;
    }

    root->appendChild(std::move(builtSlices.root));

    if (options.showChartTitle && !options.title.empty()) {
        root->appendChild(buildChartTitle(ctx, {pieStyle.layout.padding.left,
                                                options.pie.padding.top}));
    }

    result.root = std::move(root);
    result.valid = true;
    return result;
}

// ---------------------------------------------------------------- SparkBar

namespace {

void applySparkBarHoverNode(SceneNode* hoverNode, const SparkBarLayout& layout,
                            const SparkBarOptions& options, std::span<const Color> palette,
                            std::size_t hoveredItem, std::size_t hoveredSegment) {
    if (hoverNode == nullptr) {
        return;
    }
    const bool show = options.hoverBorderWidth > 0.0 && hoveredItem < layout.items.size() &&
                      hoveredSegment < layout.items[hoveredItem].segments.size();
    hoverNode->setVisible(show);
    if (!show) {
        return;
    }

    const SparkBarSegment& seg = layout.items[hoveredItem].segments[hoveredSegment];
    const double effRadius =
        math::clamp(options.cornerRadius, 0.0, std::min(seg.rect.width, seg.rect.height) * 0.5);
    const double half = options.hoverBorderWidth * 0.5;
    const double expand = std::max(0.0, half - 0.25);
    const RectD outline{seg.rect.x - expand, seg.rect.y - expand,
                        seg.rect.width + expand * 2.0, seg.rect.height + expand * 2.0};
    hoverNode->setFrame(outline);

    SceneStyle style = hoverNode->style();
    style.fill = Color{};
    style.stroke = Color::mix(resolveSeriesColor(palette, seg.seriesIndex),
                              Color::Rgb(0.0, 0.0, 0.0), 0.14);
    style.strokeWidth = options.hoverBorderWidth;
    style.cornerRadius = effRadius + expand;
    hoverNode->setStyle(style);
}

}  // namespace

SparkBarBuildResult buildSparkBar(const SparkBarData& data, Vec2d canvasSize,
                                  const SparkBarOptions& options, std::span<const Color> palette,
                                  std::size_t hoveredItem, std::size_t hoveredSegment) {
    SparkBarBuildResult result;
    result.layout = computeSparkBarLayout(canvasSize, data, options);
    result.valid = result.layout.valid;

    auto root = makeGroup("sparkbar.scene", "sparkbar.scene");
    if (!result.valid) {
        result.root = std::move(root);
        return result;
    }

    const double radius = options.cornerRadius;
    const double fontSize = options.tagFontSize > 0.0 ? options.tagFontSize : 12.0;
    const Paint tagPaint =
        options.tagColor.visible() ? Paint{options.tagColor} : Paint{ColorToken::Label};

    // 1. 每条横向百分比堆叠条 + 上方行标题
    for (std::size_t i = 0; i < result.layout.items.size(); ++i) {
        const SparkBarItem& item = result.layout.items[i];
        const std::string rowPrefix = "row." + std::to_string(i);

        if (options.showTags && !item.tagRect.empty() && item.tagIndex < data.tags.size()) {
            const std::string& tag = data.tags[item.tagIndex];
            if (!tag.empty()) {
                root->appendChild(makeText(rowPrefix + ".tag", tag, item.tagRect, fontSize,
                                           tagPaint, TextAlign::Left, TextBaseline::Middle));
            }
        }

        for (std::size_t s = 0; s < item.segments.size(); ++s) {
            const SparkBarSegment& seg = item.segments[s];
            root->appendChild(makeRect(rowPrefix + ".seg." + std::to_string(seg.seriesIndex),
                                       seg.rect, resolveSeriesColor(palette, seg.seriesIndex),
                                       radius));
        }
    }

    // 2. 常驻 Hover 外描边节点（压在所有填充段之上，hover 移动时就地 O(1) 更新）
    auto hoverNode = makeNode(NodeType::Rect, "hover.outline");
    applySparkBarHoverNode(hoverNode.get(), result.layout, options, palette, hoveredItem,
                           hoveredSegment);
    root->appendChild(std::move(hoverNode));

    // 3. 底部 GitHub 风格圆点 + 语言占比图例
    for (const SparkBarLegendItem& leg : result.layout.legend) {
        if (leg.seriesIndex >= data.series.size()) {
            continue;
        }
        const std::string keySuffix = std::to_string(leg.seriesIndex);
        auto dot = makeNode(NodeType::Ellipse, "legend.dot." + keySuffix);
        dot->setFrame(leg.dotRect);
        {
            SceneStyle style;
            style.fill = resolveSeriesColor(palette, leg.seriesIndex);
            dot->setStyle(style);
        }
        root->appendChild(std::move(dot));

        char pctBuf[32] = {};
        std::snprintf(pctBuf, sizeof(pctBuf), "  %.1f%%", leg.fraction * 100.0);
        root->appendChild(makeText("legend.text." + keySuffix,
                                   data.series[leg.seriesIndex].name + pctBuf, leg.textRect,
                                   fontSize, tagPaint, TextAlign::Left, TextBaseline::Middle));
    }

    result.root = std::move(root);
    return result;
}

void updateSparkBarHover(SceneNode& sparkRoot, const SparkBarLayout& layout,
                         const SparkBarOptions& options, std::span<const Color> palette,
                         std::size_t hoveredItem, std::size_t hoveredSegment) {
    applySparkBarHoverNode(sparkRoot.findChildByKey("hover.outline"), layout, options, palette,
                           hoveredItem, hoveredSegment);
}

// ---------------------------------------------------------------- 表格

namespace {

void setRectFill(SceneNode& node, Paint fill, double cornerRadius = 0.0) {
    SceneStyle style = node.style();
    style.fill = std::move(fill);
    style.stroke = Color{};
    style.strokeWidth = 0.0;
    style.cornerRadius = cornerRadius;
    node.setStyle(style);
}

void applyTableTextStyle(SceneNode& node, Paint fill, double fontSize, TextAlign align) {
    SceneStyle style = node.style();
    style.fill = std::move(fill);
    style.fontSize = fontSize;
    style.textAlign = align;
    style.textBaseline = TextBaseline::Middle;
    style.maxLines = 1;
    node.setStyle(style);
}

void applyTableHighlightNodes(SceneNode* hoverBg, SceneNode* selectedBg, SceneNode* selectedAccent,
                              const TableLayout& layout, Vec2d canvasSize,
                              const TableStyle& style, int hoveredPosition, int selectedPosition) {
    const double rowWidth = std::max({0.0, canvasSize.x, layout.totalWidth});
    const double rowHeight = style.metrics.rowHeight;

    const bool showHover = hoveredPosition >= layout.firstRow &&
                           hoveredPosition < layout.lastRow &&
                           hoveredPosition != selectedPosition;
    if (hoverBg != nullptr) {
        hoverBg->setVisible(showHover);
        if (showHover) {
            hoverBg->setFrame(
                {0.0, layout.rowContentTop(hoveredPosition), rowWidth, rowHeight});
        }
        setRectFill(*hoverBg, style.hoverRow);
    }

    const bool showSelected =
        selectedPosition >= layout.firstRow && selectedPosition < layout.lastRow;
    if (selectedBg != nullptr) {
        selectedBg->setVisible(showSelected);
        if (showSelected) {
            selectedBg->setFrame(
                {0.0, layout.rowContentTop(selectedPosition), rowWidth, rowHeight});
        }
        setRectFill(*selectedBg, style.selectedRow);
    }
    if (selectedAccent != nullptr) {
        selectedAccent->setVisible(showSelected);
        if (showSelected) {
            selectedAccent->setFrame(
                {0.0, layout.rowContentTop(selectedPosition), 3.0, rowHeight});
        }
        setRectFill(*selectedAccent, style.accent);
    }
}

}  // namespace

std::unique_ptr<SceneNode> buildTableChrome(const TableData& data, const TableState& state,
                                            const TableLayout& layout, Vec2d canvasSize,
                                            const TableStyle& style, double scrollX) {
    const double width = std::max(0.0, canvasSize.x);
    const double height = std::max(0.0, canvasSize.y);
    const double headerHeight = style.metrics.headerHeight;
    const double radius = style.radius;
    const double pad = style.metrics.paddingX;

    auto chrome = makeGroup("table.chrome", "table chrome");

    // 卡片底 + 表头底
    chrome->appendChild(makeRect("card", {0.0, 0.0, width, height}, style.background, radius));
    chrome->appendChild(
        makeRect("header.bg", {0.0, 0.0, width, headerHeight}, style.headerBackground, radius));
    chrome->appendChild(makeRect("header.patch", {0.0, headerHeight - radius, width, radius},
                                 style.headerBackground));

    // 表头裁剪视口 + 跟随横向滚动平移的列标题组
    auto headerViewport = makeGroup("header.viewport", "header viewport");
    headerViewport->setClipRect({0.0, 0.0, width, headerHeight});

    auto headerCells = makeGroup("header.cells", "header cells");
    headerCells->setTranslation({-scrollX, 0.0});

    for (std::size_t c = 0; c < layout.columns.size() && c < data.columns.size(); ++c) {
        const TableColumn& column = data.columns[c];
        const TableColumnSpan& span = layout.columns[c];
        const bool sorted = state.isSortedBy(column.id);
        const SortDir direction = state.directionOf(column.id);
        const double reserved = column.sortable ? 14.0 : 0.0;

        if (!column.title.empty()) {
            auto text = makeNode(NodeType::Text, "header.cell." + std::to_string(c));
            text->setText(column.title);
            text->setFrame({span.x + pad, 0.0, std::max(0.0, span.width - 2.0 * pad - reserved),
                            headerHeight});
            applyTableTextStyle(*text, sorted ? style.headerActiveText : style.headerText,
                                style.headerFontSize,
                                column.isNumeric() ? TextAlign::Right : TextAlign::Left);
            headerCells->appendChild(std::move(text));
        }

        if (sorted && direction != SortDir::None) {
            const double cx = span.x + span.width - pad - 5.0;
            const double cy = headerHeight * 0.5;
            std::vector<Vec2d> triangle =
                direction == SortDir::Asc
                    ? std::vector<Vec2d>{Vec2d{cx - 4.0, cy + 2.5}, Vec2d{cx + 4.0, cy + 2.5},
                                         Vec2d{cx, cy - 2.5}}
                    : std::vector<Vec2d>{Vec2d{cx - 4.0, cy - 2.5}, Vec2d{cx + 4.0, cy - 2.5},
                                         Vec2d{cx, cy + 2.5}};
            headerCells->appendChild(makePolygon("header.sort." + std::to_string(c),
                                                 std::move(triangle), style.accent, Color{}, 0.0));
        }
    }

    headerViewport->appendChild(std::move(headerCells));
    chrome->appendChild(std::move(headerViewport));
    chrome->appendChild(
        makeRect("header.line", {0.0, headerHeight - 1.0, width, 1.0}, style.border));

    if (layout.rowCount <= 0) {
        auto empty = makeNode(NodeType::Text, "empty");
        empty->setText(data.columns.empty() ? "no data" : "no rows");
        empty->setFrame({0.0, headerHeight, width, std::max(0.0, height - headerHeight)});
        applyTableTextStyle(*empty, style.mutedText, style.cellFontSize, TextAlign::Center);
        chrome->appendChild(std::move(empty));
    }

    return chrome;
}

std::unique_ptr<SceneNode> buildTableBody(const TableData& data, const TableState& state,
                                          const TableLayout& layout, Vec2d canvasSize,
                                          const TableStyle& style, int hoveredPosition) {
    const double rowWidth = std::max({0.0, canvasSize.x, layout.totalWidth});
    const double rowHeight = style.metrics.rowHeight;
    const double pad = style.metrics.paddingX;

    auto body = makeGroup("body", "table body");

    auto hoverBg = makeNode(NodeType::Rect, "row.hover.bg");
    auto selectedBg = makeNode(NodeType::Rect, "row.selected.bg");
    auto selectedAccent = makeNode(NodeType::Rect, "row.selected.accent");
    applyTableHighlightNodes(hoverBg.get(), selectedBg.get(), selectedAccent.get(), layout,
                             canvasSize, style, hoveredPosition, state.selectedPosition());
    body->appendChild(std::move(hoverBg));
    body->appendChild(std::move(selectedBg));
    body->appendChild(std::move(selectedAccent));

    for (int position = layout.firstRow; position < layout.lastRow; ++position) {
        const TableRow* dataRow = state.rowAt(position);
        const std::string rowKey = (dataRow != nullptr && !dataRow->key.empty())
                                       ? ("row." + dataRow->key)
                                       : ("row." + std::to_string(position));
        // 统一使用未扣滚动的逻辑内容坐标 y = position * rowHeight，
        // 交由 ScrollWidget::content 的平移矩阵统一偏移（滚动时已有行无需重算坐标）
        const double y = layout.rowContentTop(position);

        body->appendChild(
            makeRect(rowKey + ".grid", {0.0, y + rowHeight - 1.0, rowWidth, 1.0}, style.grid));

        if (dataRow == nullptr) {
            continue;
        }

        for (std::size_t c = 0; c < layout.columns.size() && c < data.columns.size(); ++c) {
            const TableColumn& column = data.columns[c];
            const TableCell* cell = dataRow->cell(c);
            if (cell == nullptr || cell->missing()) {
                continue;
            }
            const std::string value = formatTableCell(*cell, column);
            if (value.empty()) {
                continue;
            }

            const TableColumnSpan& span = layout.columns[c];
            Paint color = style.rowText;
            if (column.hasSignFormat()) {
                const double number = cell->numericValue();
                color = number > 0.0 ? style.positive
                                     : (number < 0.0 ? style.negative : style.mutedText);
            }

            auto text = makeNode(NodeType::Text, rowKey + ".cell." + std::to_string(c));
            text->setText(value);
            text->setFrame({span.x + pad, y, std::max(0.0, span.width - 2.0 * pad), rowHeight});
            applyTableTextStyle(*text, color, style.cellFontSize,
                                column.isNumeric() ? TextAlign::Right : TextAlign::Left);
            body->appendChild(std::move(text));
        }
    }

    return body;
}

void updateTableHeaderScroll(SceneNode& chromeRoot, double scrollX) noexcept {
    if (SceneNode* viewport = chromeRoot.findChildByKey("header.viewport")) {
        if (SceneNode* cells = viewport->findChildByKey("header.cells")) {
            cells->setTranslation({-scrollX, 0.0});
        }
    }
}

std::unique_ptr<SceneNode> buildTable(const TableData& data, const TableState& state,
                                      const TableLayout& layout, Vec2d canvasSize,
                                      const TableStyle& style, int hoveredPosition) {
    const double width = std::max(0.0, canvasSize.x);
    const double height = std::max(0.0, canvasSize.y);
    const double headerHeight = style.metrics.headerHeight;
    const double radius = style.radius;

    auto plot = makeGroup("table", "table");
    plot->appendChild(buildTableChrome(data, state, layout, canvasSize, style, 0.0));

    auto bodyViewport = makeGroup("body.viewport", "body viewport");
    bodyViewport->setTranslation({0.0, headerHeight});
    bodyViewport->setClipRect({0.0, 0.0, width, std::max(0.0, height - headerHeight)});

    auto body = buildTableBody(data, state, layout, canvasSize, style, hoveredPosition);
    body->setTranslation({0.0, -layout.scroll});
    bodyViewport->appendChild(std::move(body));
    plot->appendChild(std::move(bodyViewport));

    auto border = makeNode(NodeType::Rect, "frame.border");
    border->setFrame({0.0, 0.0, width, height});
    {
        SceneStyle borderStyle;
        borderStyle.stroke = style.border;
        borderStyle.strokeWidth = 1.0;
        borderStyle.cornerRadius = radius;
        border->setStyle(borderStyle);
    }
    plot->appendChild(std::move(border));
    return plot;
}

void updateTableHighlights(SceneNode& bodyOrTableRoot, const TableLayout& layout, Vec2d canvasSize,
                           const TableStyle& style, int hoveredPosition, int selectedPosition) {
    SceneNode* body = &bodyOrTableRoot;
    if (body->key() != "body") {
        if (SceneNode* direct = bodyOrTableRoot.findChildByKey("body")) {
            body = direct;
        } else if (SceneNode* vp = bodyOrTableRoot.findChildByKey("body.viewport")) {
            body = vp->findChildByKey("body");
        } else {
            body = nullptr;
        }
    }
    if (body == nullptr) {
        return;
    }
    applyTableHighlightNodes(body->findChildByKey("row.hover.bg"),
                             body->findChildByKey("row.selected.bg"),
                             body->findChildByKey("row.selected.accent"), layout, canvasSize, style,
                             hoveredPosition, selectedPosition);
}

TableBuildResult buildTable(const TableData& data, const TableState& state, Vec2d canvasSize,
                            double scrollOffset, const TableStyle& style, int hoveredPosition,
                            std::span<const double> cachedColumnWidths,
                            const TextMeasureFn& textMeasure) {
    TableBuildResult result;
    if (cachedColumnWidths.size() == data.columns.size() && !data.columns.empty()) {
        result.columnWidths.assign(cachedColumnWidths.begin(), cachedColumnWidths.end());
    } else {
        result.columnWidths =
            measureTableColumns(data, canvasSize.x, style.headerFontSize, style.cellFontSize,
                                style.metrics.paddingX, textMeasure);
    }

    TableLayoutInput input;
    input.columnWidths = result.columnWidths;
    input.rowCount = static_cast<int>(state.rowCount());
    input.viewWidth = canvasSize.x;
    input.viewHeight = std::max(0.0, canvasSize.y - style.metrics.headerHeight);
    input.scrollOffset = scrollOffset;
    input.metrics = style.metrics;
    result.layout = computeTableLayout(input);

    result.root = buildTable(data, state, result.layout, canvasSize, style, hoveredPosition);
    return result;
}

// ---------------------------------------------------------------- 滚动条描述树

namespace {

[[nodiscard]] const Paint& pickThumbPaint(const ScrollBarStyle& style, bool hovered,
                                          bool active) noexcept {
    if (active && style.thumbActiveFill.visible()) {
        return style.thumbActiveFill;
    }
    if (hovered && style.thumbHoverFill.visible()) {
        return style.thumbHoverFill;
    }
    return style.thumbFill;
}

void applyScrollChromeNodes(SceneNode* vTrack, SceneNode* vThumb, SceneNode* hTrack,
                            SceneNode* hThumb, SceneNode* corner, SceneNode* border,
                            const ScrollLayout& layout, Vec2d containerSize,
                            const ScrollBarStyle& style, const ScrollBarVisualState& state) {
    const double barRadius = std::max(0.0, style.metrics.cornerRadius);

    // 纵向滚动槽与滑块
    const bool showV = layout.vertical.visible && !layout.vertical.track.empty();
    if (vTrack != nullptr) {
        vTrack->setVisible(showV && style.trackFill.visible());
        if (showV) {
            vTrack->setFrame(layout.vertical.track);
        }
        SceneStyle s = vTrack->style();
        s.fill = style.trackFill;
        s.cornerRadius = barRadius;
        vTrack->setStyle(s);
    }
    if (vThumb != nullptr) {
        const bool showVThumb = showV && !layout.vertical.thumb.empty();
        vThumb->setVisible(showVThumb);
        if (showVThumb) {
            vThumb->setFrame(layout.vertical.thumb);
        }
        SceneStyle s = vThumb->style();
        s.fill = pickThumbPaint(style, state.verticalHovered, state.verticalActive);
        s.cornerRadius = barRadius;
        s.cursor = layout.vertical.scrollable ? CursorShape::Pointer : CursorShape::Default;
        vThumb->setStyle(s);
    }

    // 横向滚动槽与滑块
    const bool showH = layout.horizontal.visible && !layout.horizontal.track.empty();
    if (hTrack != nullptr) {
        hTrack->setVisible(showH && style.trackFill.visible());
        if (showH) {
            hTrack->setFrame(layout.horizontal.track);
        }
        SceneStyle s = hTrack->style();
        s.fill = style.trackFill;
        s.cornerRadius = barRadius;
        hTrack->setStyle(s);
    }
    if (hThumb != nullptr) {
        const bool showHThumb = showH && !layout.horizontal.thumb.empty();
        hThumb->setVisible(showHThumb);
        if (showHThumb) {
            hThumb->setFrame(layout.horizontal.thumb);
        }
        SceneStyle s = hThumb->style();
        s.fill = pickThumbPaint(style, state.horizontalHovered, state.horizontalActive);
        s.cornerRadius = barRadius;
        s.cursor = layout.horizontal.scrollable ? CursorShape::Pointer : CursorShape::Default;
        hThumb->setStyle(s);
    }

    // 右下角交汇区
    if (corner != nullptr) {
        const bool showCorner =
            !layout.corner.empty() && style.cornerFill.visible();
        corner->setVisible(showCorner);
        if (showCorner) {
            corner->setFrame(layout.corner);
        }
        SceneStyle s = corner->style();
        s.fill = style.cornerFill;
        corner->setStyle(s);
    }

    // 容器外边框
    if (border != nullptr) {
        const double w = std::max(0.0, containerSize.x);
        const double h = std::max(0.0, containerSize.y);
        const bool showBorder =
            w > 0.0 && h > 0.0 && style.borderWidth > 0.0 && style.border.visible();
        border->setVisible(showBorder);
        if (showBorder) {
            border->setFrame({0.0, 0.0, w, h});
        }
        SceneStyle s = border->style();
        s.fill = Paint{};
        s.stroke = style.border;
        s.strokeWidth = style.borderWidth;
        s.cornerRadius = std::max(0.0, style.containerRadius);
        border->setStyle(s);
    }
}

}  // namespace

std::unique_ptr<SceneNode> buildScrollBars(const ScrollLayout& layout, Vec2d containerSize,
                                           const ScrollBarStyle& style,
                                           const ScrollBarVisualState& state) {
    auto chrome = makeGroup("scroll.chrome", "scroll chrome");

    auto vTrack = makeNode(NodeType::Rect, "scroll.v.track", "vertical scrollbar track");
    auto vThumb = makeNode(NodeType::Rect, "scroll.v.thumb", "vertical scrollbar thumb");
    auto hTrack = makeNode(NodeType::Rect, "scroll.h.track", "horizontal scrollbar track");
    auto hThumb = makeNode(NodeType::Rect, "scroll.h.thumb", "horizontal scrollbar thumb");
    auto corner = makeNode(NodeType::Rect, "scroll.corner", "scrollbar corner");
    auto border = makeNode(NodeType::Rect, "scroll.border", "scroll container border");

    applyScrollChromeNodes(vTrack.get(), vThumb.get(), hTrack.get(), hThumb.get(),
                           corner.get(), border.get(), layout, containerSize, style, state);

    chrome->appendChild(std::move(vTrack));
    chrome->appendChild(std::move(vThumb));
    chrome->appendChild(std::move(hTrack));
    chrome->appendChild(std::move(hThumb));
    chrome->appendChild(std::move(corner));
    chrome->appendChild(std::move(border));
    return chrome;
}

void updateScrollBars(SceneNode& chromeRoot, const ScrollLayout& layout, Vec2d containerSize,
                      const ScrollBarStyle& style, const ScrollBarVisualState& state) {
    applyScrollChromeNodes(chromeRoot.findChildByKey("scroll.v.track"),
                           chromeRoot.findChildByKey("scroll.v.thumb"),
                           chromeRoot.findChildByKey("scroll.h.track"),
                           chromeRoot.findChildByKey("scroll.h.thumb"),
                           chromeRoot.findChildByKey("scroll.corner"),
                           chromeRoot.findChildByKey("scroll.border"),
                           layout, containerSize, style, state);
}

// ---------------------------------------------------------------- 统一入口

SceneBuildResult buildScene(const ChartSpec& spec) {
    switch (spec.kind) {
        case ChartKind::Bar:
            return buildBarChart(spec.dataset, spec.canvasSize, spec.layout, spec.options);
        case ChartKind::Pie:
            return buildPieChart(spec.pie, spec.canvasSize, spec.options);
        case ChartKind::Line:
            break;
    }
    return buildLineChart(spec.dataset, spec.canvasSize, spec.layout, spec.options);
}

SceneDiffResult patchScene(SceneNode& target, const SceneBuildResult& description) {
    if (!description.root) {
        return {};
    }
    return reconcileScene(target, *description.root);
}

}  // namespace kiriyama::core
