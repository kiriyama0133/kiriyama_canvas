#include "kiriyama/core/SceneBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <utility>

#include "kiriyama/math/MathUtils.h"

namespace kiriyama::core {
namespace {

using math::kHalfPi;
using math::kTwoPi;

/// 网格线贴到绘图区边缘 0.5px 内就不再画，否则和轴线重叠成一条粗线。
constexpr double kEdgeTolerance = 0.5;

/// 曲线细分的总点数上限，防止 10 万个点 × 16 段把 wasm 内存吃穿。
constexpr std::size_t kMaxCurvePoints = 4000;

// ---------------------------------------------------------------- 主题

const Color& accentOf(const std::vector<Color>& palette, std::size_t index) {
    return palette[index % palette.size()];
}

// ---------------------------------------------------------------- 节点工厂

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

std::unique_ptr<SceneNode> makeRect(std::string key, const RectD& frame, const Color& fill,
                                    double cornerRadius = 0.0, std::string name = {}) {
    auto node = makeNode(NodeType::Rect, std::move(key), std::move(name));
    node->setFrame(frame);
    SceneStyle style;
    style.fill = fill;
    style.cornerRadius = cornerRadius;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makeLine(std::string key, Vec2d from, Vec2d to, const Color& stroke,
                                    double width) {
    auto node = makeNode(NodeType::Line, std::move(key));
    node->setPoints({from, to});
    SceneStyle style;
    style.stroke = stroke;
    style.strokeWidth = width;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makePolyline(std::string key, std::vector<Vec2d> points,
                                        const Color& stroke, double width) {
    auto node = makeNode(NodeType::Polyline, std::move(key));
    node->setPoints(std::move(points));
    SceneStyle style;
    style.stroke = stroke;
    style.strokeWidth = width;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makePolygon(std::string key, std::vector<Vec2d> points,
                                       const Color& fill, const Color& stroke,
                                       double strokeWidth) {
    auto node = makeNode(NodeType::Polygon, std::move(key));
    node->setPoints(std::move(points));
    SceneStyle style;
    style.fill = fill;
    style.stroke = stroke;
    style.strokeWidth = strokeWidth;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makeEllipse(std::string key, Vec2d center, double radius,
                                       const Color& fill) {
    auto node = makeNode(NodeType::Ellipse, std::move(key));
    node->setFrame({center.x - radius, center.y - radius, radius * 2.0, radius * 2.0});
    SceneStyle style;
    style.fill = fill;
    node->setStyle(style);
    return node;
}

std::unique_ptr<SceneNode> makeText(std::string key, std::string text, const RectD& frame,
                                    double fontSize, const Color& color, TextAlign align,
                                    TextBaseline baseline) {
    auto node = makeNode(NodeType::Text, std::move(key));
    node->setText(std::move(text));
    node->setFrame(frame);
    SceneStyle style;
    style.fill = color;
    style.fontSize = fontSize;
    style.textAlign = align;
    style.textBaseline = baseline;
    node->setStyle(style);
    return node;
}

// ---------------------------------------------------------------- 文本

TextMetrics measureText(const LayoutOptions& layoutOptions, std::string_view text,
                        double fontSize) {
    if (text.empty()) {
        return {};
    }
    TextMetrics metrics = layoutOptions.measureText
                              ? layoutOptions.measureText(text, fontSize)
                              : defaultTextMeasure(text, fontSize);
    if (!(metrics.height > 0.0)) {
        metrics.height = fontSize * 1.25;
    }
    return metrics;
}

/// 把锚点（对齐基准点）换成排版盒。
RectD frameAt(Vec2d anchor, const TextMetrics& metrics, TextAlign horizontal,
              TextBaseline vertical) {
    double x = anchor.x;
    if (horizontal == TextAlign::Center) {
        x -= metrics.width * 0.5;
    } else if (horizontal == TextAlign::Right) {
        x -= metrics.width;
    }
    double y = anchor.y;
    if (vertical == TextBaseline::Middle) {
        y -= metrics.height * 0.5;
    } else if (vertical == TextBaseline::Bottom) {
        y -= metrics.height;
    }
    return {x, y, metrics.width, metrics.height};
}

/// 软夹取：标签整体推出画布时拉回来，不裁切文字。
RectD clampToCanvas(const RectD& frame, const RectD& canvas, double margin = 2.0) {
    if (frame.width <= 0.0 || canvas.width <= 0.0) {
        return frame;
    }
    double x = frame.x;
    if (x + frame.width > canvas.right() - margin) {
        x = canvas.right() - margin - frame.width;
    }
    if (x < margin) {
        x = margin;
    }
    double y = frame.y;
    if (y + frame.height > canvas.bottom() - margin) {
        y = canvas.bottom() - margin - frame.height;
    }
    if (y < margin) {
        y = margin;
    }
    return {x, y, frame.width, frame.height};
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

// ---------------------------------------------------------------- 数据辅助

DataIssues countIssues(const Dataset& dataset) {
    DataIssues issues;
    for (const Series& series : dataset.series) {
        if (series.points.empty()) {
            ++issues.emptySeries;
            continue;
        }
        for (const DataPoint& point : series.points) {
            if (!point.hasValue) {
                ++issues.nullValues;
            } else if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                ++issues.nonFiniteValues;
            }
        }
    }
    return issues;
}

DataIssues countIssues(const PieChartData& data) {
    DataIssues issues;
    for (const PieSlice& slice : data.slices) {
        if (!std::isfinite(slice.value)) {
            ++issues.nonFiniteValues;
        } else if (slice.value < 0.0) {
            ++issues.negativeValues;
        }
    }
    return issues;
}

DataBounds boundsOfSeries(const Dataset& dataset, std::size_t first, std::size_t count) {
    DataBounds bounds;
    for (std::size_t i = first; i < first + count && i < dataset.series.size(); ++i) {
        for (const DataPoint& point : dataset.series[i].points) {
            if (!point.valid()) {
                continue;
            }
            if (!bounds.hasData) {
                bounds.x = RangeD{point.x, point.x};
                bounds.y = RangeD{point.y, point.y};
                bounds.hasData = true;
            } else {
                bounds.x.expandToInclude(point.x);
                bounds.y.expandToInclude(point.y);
            }
        }
    }
    return bounds;
}

/// 系列 key：名字唯一时用名字，否则用下标。
/// 两个同名系列共用一个 key 会让 diff 把节点配错，宁可用下标。
std::vector<std::string> makeSeriesKeys(const Dataset& dataset) {
    std::vector<std::string> keys;
    keys.reserve(dataset.series.size());
    for (std::size_t i = 0; i < dataset.series.size(); ++i) {
        const std::string& name = dataset.series[i].name;
        bool unique = !name.empty();
        if (unique) {
            for (std::size_t j = 0; j < dataset.series.size(); ++j) {
                if (j != i && dataset.series[j].name == name) {
                    unique = false;
                    break;
                }
            }
        }
        keys.push_back(unique ? "series:" + name : "series#" + std::to_string(i));
    }
    return keys;
}

// ---------------------------------------------------------------- 曲线

/// 单调三次 Hermite（Fritsch–Carlson）。x 不严格递增时退回折线——
/// 数据里 x 回绕（时间轴乱序）是常见脏数据，硬插值会画出自交的怪线。
std::vector<Vec2d> monotoneCurve(std::span<const Vec2d> points, int segments) {
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
    if (spans * perSpan > kMaxCurvePoints) {
        perSpan = std::max<std::size_t>(1, kMaxCurvePoints / spans);
    }
    if (perSpan <= 1) {
        return {points.begin(), points.end()};
    }

    std::vector<double> delta(spans);
    for (std::size_t i = 0; i < spans; ++i) {
        delta[i] = (points[i + 1].y - points[i].y) / (points[i + 1].x - points[i].x);
    }

    std::vector<double> slope(n);
    slope[0] = delta[0];
    slope[n - 1] = delta[spans - 1];
    for (std::size_t i = 1; i + 1 < n; ++i) {
        // 极值点处切线取 0，曲线才不会冲过数据点。
        slope[i] = delta[i - 1] * delta[i] <= 0.0 ? 0.0 : (delta[i - 1] + delta[i]) * 0.5;
    }

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
                             (-2.0 * t3 + 3.0 * t2) * points[i + 1].y + (t3 - t2) * m1;
            out.push_back({points[i].x + h * t, y});
        }
    }
    return out;
}

// ---------------------------------------------------------------- 饼图几何
// sectorPolygon 已提升为 core 公共几何（Layout.h）——
// SceneBuilder 建描述树和渲染层画扇形共用一份，保证描的画的同一个形状。

// ---------------------------------------------------------------- 组装

std::unique_ptr<SceneNode> buildGrid(const ChartLayout& layout, const SceneBuildOptions& options,
                                     bool categoryAxis) {
    auto group = makeGroup("grid", "grid");
    if (!options.showGrid) {
        return group;
    }

    const ChartTheme& theme = options.theme;
    int index = 0;
    if (layout.xAxis.visible && (!categoryAxis || options.showCategoryGrid)) {
        for (const TickMark& tick : layout.xAxis.ticks) {
            const double x = tick.position;
            if (std::abs(x - layout.plot.left()) < kEdgeTolerance ||
                std::abs(x - layout.plot.right()) < kEdgeTolerance) {
                continue;
            }
            group->appendChild(makeLine("grid.x." + std::to_string(index++),
                                        {x, layout.plot.top()}, {x, layout.plot.bottom()},
                                        theme.grid, theme.gridWidth));
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
                                        theme.grid, theme.gridWidth));
        }
    }
    return group;
}

std::unique_ptr<SceneNode> buildAxisTexts(const ChartLayout& layout,
                                          const LayoutOptions& layoutOptions,
                                          const SceneBuildOptions& options, Vec2d canvasSize) {
    auto group = makeGroup("axis.labels", "axis labels");
    if (!options.showAxisLabels) {
        return group;
    }
    const ChartTheme& theme = options.theme;

    if (layout.xAxis.visible) {
        const double fontSize = layoutOptions.x.labelFontSize;
        const double gap = layoutOptions.x.labelGap;
        for (std::size_t i = 0; i < layout.xAxis.ticks.size(); ++i) {
            const TickMark& tick = layout.xAxis.ticks[i];
            if (tick.label.empty()) {
                continue;
            }
            const TextMetrics metrics = measureText(layoutOptions, tick.label, fontSize);
            const RectD frame = frameAt({tick.position, layout.plot.bottom() + gap}, metrics,
                                        TextAlign::Center, TextBaseline::Top);
            group->appendChild(makeText("axis.x.tick." + std::to_string(i), tick.label, frame,
                                        fontSize, theme.label, TextAlign::Center,
                                        TextBaseline::Top));
        }
    }

    if (layout.yAxis.visible) {
        const double fontSize = layoutOptions.y.labelFontSize;
        const double gap = layoutOptions.y.labelGap;
        for (std::size_t i = 0; i < layout.yAxis.ticks.size(); ++i) {
            const TickMark& tick = layout.yAxis.ticks[i];
            if (tick.label.empty()) {
                continue;
            }
            const TextMetrics metrics = measureText(layoutOptions, tick.label, fontSize);
            const RectD frame = frameAt({layout.plot.left() - gap, tick.position}, metrics,
                                        TextAlign::Right, TextBaseline::Middle);
            group->appendChild(makeText("axis.y.tick." + std::to_string(i), tick.label, frame,
                                        fontSize, theme.label, TextAlign::Right,
                                        TextBaseline::Middle));
        }
    }

    if (layout.hasSecondaryY && layout.secondaryYAxis.visible) {
        const double fontSize = layoutOptions.secondaryY.labelFontSize;
        const double gap = layoutOptions.plotGap;
        for (std::size_t i = 0; i < layout.secondaryYAxis.ticks.size(); ++i) {
            const TickMark& tick = layout.secondaryYAxis.ticks[i];
            if (tick.label.empty()) {
                continue;
            }
            const TextMetrics metrics = measureText(layoutOptions, tick.label, fontSize);
            const RectD frame = frameAt({layout.plot.right() + gap, tick.position}, metrics,
                                        TextAlign::Left, TextBaseline::Middle);
            group->appendChild(makeText("axis.y2.tick." + std::to_string(i), tick.label, frame,
                                        fontSize, theme.label, TextAlign::Left,
                                        TextBaseline::Middle));
        }
    }

    (void)canvasSize;
    return group;
}

std::unique_ptr<SceneNode> buildAxisTitles(const ChartLayout& layout,
                                           const LayoutOptions& layoutOptions,
                                           const SceneBuildOptions& options, Vec2d canvasSize) {
    auto group = makeGroup("axis.titles", "axis titles");
    if (!options.showAxisTitles) {
        return group;
    }
    const ChartTheme& theme = options.theme;

    if (layout.xAxis.visible && !layoutOptions.x.title.empty()) {
        const double fontSize = layoutOptions.x.titleFontSize;
        const TextMetrics metrics = measureText(layoutOptions, layoutOptions.x.title, fontSize);
        const double y = std::max(layout.plot.bottom() + fontSize * 0.75,
                                  canvasSize.y - layoutOptions.padding.bottom - fontSize * 0.75);
        const RectD frame = frameAt({layout.plot.center().x, y}, metrics, TextAlign::Center,
                                    TextBaseline::Middle);
        group->appendChild(makeText("axis.x.title", layoutOptions.x.title, frame, fontSize,
                                    theme.label, TextAlign::Center, TextBaseline::Middle));
    }

    if (layout.yAxis.visible && !layoutOptions.y.title.empty()) {
        const double fontSize = layoutOptions.y.titleFontSize;
        const TextMetrics metrics = measureText(layoutOptions, layoutOptions.y.title, fontSize);
        // 旋转 -90°：frame 放在原点，再把盒子中心搬到目标位置。
        const Vec2d center{layoutOptions.padding.left + fontSize * 0.75,
                           layout.plot.center().y};
        auto node = makeText("axis.y.title", layoutOptions.y.title,
                             {0.0, 0.0, metrics.width, metrics.height}, fontSize, theme.label,
                             TextAlign::Center, TextBaseline::Middle);
        node->setTransform(Affine2D::Translation(center) * Affine2D::Rotation(-kHalfPi) *
                           Affine2D::Translation({-metrics.width * 0.5, -metrics.height * 0.5}));
        group->appendChild(std::move(node));
    }

    if (layout.hasSecondaryY && layoutOptions.secondaryY.visible &&
        !layoutOptions.secondaryY.title.empty()) {
        const double fontSize = layoutOptions.secondaryY.titleFontSize;
        const TextMetrics metrics =
            measureText(layoutOptions, layoutOptions.secondaryY.title, fontSize);
        const Vec2d center{canvasSize.x - layoutOptions.padding.right - fontSize * 0.75,
                           layout.plot.center().y};
        auto node = makeText("axis.y2.title", layoutOptions.secondaryY.title,
                             {0.0, 0.0, metrics.width, metrics.height}, fontSize, theme.label,
                             TextAlign::Center, TextBaseline::Middle);
        node->setTransform(Affine2D::Translation(center) * Affine2D::Rotation(kHalfPi) *
                           Affine2D::Translation({-metrics.width * 0.5, -metrics.height * 0.5}));
        group->appendChild(std::move(node));
    }
    return group;
}

std::unique_ptr<SceneNode> buildAxisLines(const ChartLayout& layout,
                                          const SceneBuildOptions& options) {
    auto group = makeGroup("axis.lines", "axis lines");
    if (!options.showAxisLines) {
        return group;
    }
    const ChartTheme& theme = options.theme;
    if (layout.xAxis.visible) {
        group->appendChild(makeLine("axis.x.line", {layout.plot.left(), layout.plot.bottom()},
                                    {layout.plot.right(), layout.plot.bottom()},
                                    theme.axisLine, theme.axisWidth));
    }
    if (layout.yAxis.visible) {
        group->appendChild(makeLine("axis.y.line", {layout.plot.left(), layout.plot.top()},
                                    {layout.plot.left(), layout.plot.bottom()},
                                    theme.axisLine, theme.axisWidth));
    }
    if (layout.hasSecondaryY && layout.secondaryYAxis.visible) {
        group->appendChild(makeLine("axis.y2.line", {layout.plot.right(), layout.plot.top()},
                                    {layout.plot.right(), layout.plot.bottom()},
                                    theme.axisLine, theme.axisWidth));
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
                                           const SceneBuildOptions& options) {
    auto group = makeGroup("series", "series");
    const ChartTheme& theme = options.theme;
    const std::vector<std::string> keys = makeSeriesKeys(dataset);

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
        const Color color = theme.seriesColor(s);

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
                    ? monotoneCurve(raw, options.curveSegments)
                    : raw;

            if (options.showArea && curve.size() >= 2) {
                std::vector<Vec2d> area = curve;
                area.push_back({curve.back().x, areaBaseline});
                area.push_back({curve.front().x, areaBaseline});
                Color areaFill = color;
                areaFill.a *= static_cast<float>(math::saturate(options.areaOpacity));
                seriesNode->appendChild(makePolygon(segmentKey + ".area", std::move(area),
                                                    areaFill, Color{}, 0.0));
            }

            if (options.lineWidth > 0.0 && curve.size() >= 2) {
                seriesNode->appendChild(
                    makePolyline(segmentKey + ".line", curve, color, options.lineWidth));
            }

            // 孤立点：折线只有两个端点都画不出来，给它一个点。
            if (curve.size() == 1 && !options.showPoints && options.pointRadius > 0.0) {
                seriesNode->appendChild(makeEllipse(segmentKey + ".dot", curve.front(),
                                                     std::max(options.pointRadius, 1.5), color));
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
                                                     options.pointRadius, color));
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
                                     const LayoutOptions& layoutOptions,
                                     const SceneBuildOptions& options) {
    auto group = makeGroup("bars", "bars");
    const ChartTheme& theme = options.theme;

    const float alpha = static_cast<float>(math::saturate(options.barOpacity));
    for (const BarRect& bar : bars) {
        const std::string key = "bar." + std::to_string(bar.seriesIndex) + "." +
                                std::to_string(bar.categoryIndex);
        Color fill = theme.seriesColor(bar.seriesIndex);
        fill.a *= alpha;
        group->appendChild(
            makeRect(key, bar.rect, fill, options.barCornerRadius, "bar"));
    }

    if (!options.showValueLabels) {
        return group;
    }

    // 数值标签单独一组，叠在柱子上方，避免和柱子一起被裁剪。
    auto labels = makeGroup("bar.labels", "bar value labels");
    const double fontSize = theme.labelFontSize;
    const double dataSpan = std::max(1e-9, layout.yAxis.domain.span());
    for (const BarRect& bar : bars) {
        const std::string text = formatValueLabel(bar.value, dataSpan);
        const TextMetrics metrics = measureText(layoutOptions, text, fontSize);
        if (metrics.width <= 0.0 || bar.rect.width < 6.0 ||
            bar.rect.height < metrics.height + 6.0) {
            continue;
        }
        const bool positive = bar.value >= 0.0;
        const Vec2d anchor{bar.rect.center().x,
                           positive ? bar.rect.top() - 4.0 : bar.rect.bottom() + 4.0};
        const TextBaseline baseline = positive ? TextBaseline::Bottom : TextBaseline::Top;
        const RectD frame = frameAt(anchor, metrics, TextAlign::Center, baseline);
        labels->appendChild(makeText("bar.value." + std::to_string(bar.seriesIndex) + "." +
                                         std::to_string(bar.categoryIndex),
                                     text, frame, fontSize, theme.label, TextAlign::Center,
                                     baseline));
    }
    group->appendChild(std::move(labels));
    return group;
}

std::unique_ptr<SceneNode> buildChartTitle(const SceneBuildOptions& options, Vec2d topLeft,
                                           const LayoutOptions& measureOptions) {
    const ChartTheme& theme = options.theme;
    const TextMetrics metrics = measureText(measureOptions, options.title, theme.titleFontSize);
    const RectD frame = frameAt(topLeft, metrics, TextAlign::Left, TextBaseline::Top);
    return makeText("title", options.title, frame, theme.titleFontSize, theme.title,
                    TextAlign::Left, TextBaseline::Top);
}

std::unique_ptr<SceneNode> buildEmptyText(const SceneBuildOptions& options, Vec2d canvasSize,
                                          const LayoutOptions& measureOptions) {
    auto group = makeGroup("empty", "empty");
    if (!options.showEmptyText || options.emptyText.empty()) {
        return group;
    }
    const TextMetrics metrics =
        measureText(measureOptions, options.emptyText, options.theme.labelFontSize);
    const Vec2d center{canvasSize.x * 0.5, canvasSize.y * 0.5};
    const RectD frame =
        frameAt(center, metrics, TextAlign::Center, TextBaseline::Middle);
    group->appendChild(makeText("empty.text", options.emptyText, frame,
                                options.theme.labelFontSize, options.theme.emptyText,
                                TextAlign::Center, TextBaseline::Middle));
    return group;
}

}  // namespace

// ---------------------------------------------------------------- 主题

const std::vector<Color>& defaultSeriesPalette() {
    static const std::vector<Color> palette = {
        Color::FromBytes(0x3E, 0x6D, 0xB5),  // 蓝
        Color::FromBytes(0x2E, 0x8A, 0x85),  // 青
        Color::FromBytes(0xC2, 0x76, 0x2A),  // 橙
        Color::FromBytes(0x7A, 0x5A, 0xA6),  // 紫
        Color::FromBytes(0xC0, 0x50, 0x4D),  // 红
        Color::FromBytes(0x4F, 0x8A, 0x3D),  // 绿
        Color::FromBytes(0x6B, 0x72, 0x80),  // 灰蓝
        Color::FromBytes(0xB4, 0x5C, 0x7F),  // 玫红
    };
    return palette;
}

ChartTheme ChartTheme::Light() {
    ChartTheme theme;
    theme.series = defaultSeriesPalette();
    return theme;
}

ChartTheme ChartTheme::Dark() {
    ChartTheme theme;
    theme.background = Color::FromBytes(0x1C, 0x1C, 0x1E);
    theme.plotBackground = Color::FromBytes(0x24, 0x24, 0x27);
    theme.grid = Color::FromBytes(0x3A, 0x3A, 0x3F);
    theme.axisLine = Color::FromBytes(0x55, 0x55, 0x5C);
    theme.label = Color::FromBytes(0xA8, 0xA8, 0xB0);
    theme.title = Color::FromBytes(0xEC, 0xEC, 0xF0);
    theme.emptyText = Color::FromBytes(0x77, 0x77, 0x80);
    theme.series = {
        Color::FromBytes(0x6F, 0xA8, 0xDC),
        Color::FromBytes(0x4F, 0xB3, 0xAC),
        Color::FromBytes(0xE0, 0xA4, 0x58),
        Color::FromBytes(0xA9, 0x8B, 0xD6),
        Color::FromBytes(0xE0, 0x7A, 0x75),
        Color::FromBytes(0x7D, 0xB8, 0x6A),
        Color::FromBytes(0x96, 0xA0, 0xAE),
        Color::FromBytes(0xD9, 0x8B, 0xAB),
    };
    return theme;
}

Color ChartTheme::seriesColor(std::size_t index) const {
    if (!series.empty()) {
        return accentOf(series, index);
    }
    return accentOf(defaultSeriesPalette(), index);
}

// ---------------------------------------------------------------- 折线 / 柱状

namespace {

SceneBuildResult buildCartesian(const Dataset& dataset, Vec2d canvasSize,
                                const LayoutOptions& layoutOptions,
                                const SceneBuildOptions& options, ChartKind kind) {
    SceneBuildResult result;
    result.issues = countIssues(dataset);

    const ChartTheme& theme = options.theme;
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
        const double titleHeight =
            measureText(lo, options.title, theme.titleFontSize).height;
        lo.padding.top += titleHeight + options.titleGap;
    }

    const BarMode barMode = kind == ChartKind::Bar ? options.barMode : BarMode::None;
    const DataBounds bounds = computeBounds(dataset, barMode);
    const DataBounds secondaryBounds =
        wantsSecondary ? boundsOfSeries(dataset, dataset.series.size() - secondaryCount,
                                        secondaryCount)
                       : DataBounds{};

    result.layout = wantsSecondary ? computeLayout(canvasSize, bounds, secondaryBounds, lo)
                                   : computeLayout(canvasSize, bounds, lo);
    const ChartLayout& layout = result.layout;

    auto root = makeGroup("chart", "chart");
    root->appendChild(makeRect("background", {0.0, 0.0, canvasSize.x, canvasSize.y},
                               theme.background));

    const bool hasData = dataset.validCount() > 0;
    if (!hasData || !layout.valid) {
        if (!hasData) {
            root->appendChild(buildEmptyText(options, canvasSize, lo));
        }
        result.root = std::move(root);
        result.valid = false;
        return result;
    }

    if (theme.plotBackground.visible()) {
        root->appendChild(makeRect("plot.bg", layout.plot, theme.plotBackground));
    }

    root->appendChild(buildGrid(layout, options, kind == ChartKind::Bar));

    if (kind == ChartKind::Bar) {
        result.bars = computeBarRects(dataset, layout, options.barMode, options.barOptions);
        root->appendChild(buildBars(result.bars, layout, lo, options));
    } else {
        root->appendChild(buildLineSeries(dataset, layout, options));
    }

    root->appendChild(buildAxisLines(layout, options));
    root->appendChild(buildAxisTexts(layout, lo, options, canvasSize));
    root->appendChild(buildAxisTitles(layout, lo, options, canvasSize));

    if (options.showChartTitle && !options.title.empty()) {
        root->appendChild(buildChartTitle(options, {lo.padding.left, titleTop}, lo));
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

SceneBuildResult buildPieChart(const PieChartData& data, Vec2d canvasSize,
                               const SceneBuildOptions& options) {
    SceneBuildResult result;
    result.issues = countIssues(data);

    const ChartTheme& theme = options.theme;
    canvasSize = {std::max(0.0, canvasSize.x), std::max(0.0, canvasSize.y)};

    LayoutOptions measureOptions;
    PieLayoutOptions pieOptions = options.pie;
    if (options.showChartTitle && !options.title.empty()) {
        pieOptions.padding.top +=
            measureText(measureOptions, options.title, theme.titleFontSize).height +
            options.titleGap;
    }

    result.pie = computePieLayout(canvasSize, data, pieOptions);
    const PieLayout& layout = result.pie;

    auto root = makeGroup("chart", "chart");
    root->appendChild(makeRect("background", {0.0, 0.0, canvasSize.x, canvasSize.y},
                               theme.background));

    if (!layout.valid) {
        root->appendChild(buildEmptyText(options, canvasSize, measureOptions));
        result.root = std::move(root);
        result.valid = false;
        return result;
    }

    auto sectors = makeGroup("slices", "slices");
    const Color gapColor = theme.background;
    for (const PieSector& sector : layout.sectors) {
        const std::size_t index = sector.sliceIndex;
        const Color fill = theme.seriesColor(index);
        std::vector<Vec2d> points = sectorPolygon(sector, layout, options.pieSegments);
        sectors->appendChild(
            makePolygon("slice." + std::to_string(index), std::move(points), fill, gapColor,
                        std::max(0.0, options.pieSliceGap)));

        if (!options.showPieLabels || index >= data.slices.size()) {
            continue;
        }
        const PieSlice& slice = data.slices[index];
        std::string text = slice.label;
        if (options.showPieValues) {
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

        const TextMetrics metrics = measureText(measureOptions, text, theme.labelFontSize);
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
            clampToCanvas(frameAt(anchor, metrics, align, TextBaseline::Middle),
                          layout.canvas);
        sectors->appendChild(makeText("slice.label." + std::to_string(index), text, frame,
                                      theme.labelFontSize, theme.label, align,
                                      TextBaseline::Middle));
    }
    root->appendChild(std::move(sectors));

    if (options.showChartTitle && !options.title.empty()) {
        root->appendChild(buildChartTitle(options, {pieOptions.padding.left,
                                                    options.pie.padding.top},
                                          measureOptions));
    }

    result.root = std::move(root);
    result.valid = true;
    return result;
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
