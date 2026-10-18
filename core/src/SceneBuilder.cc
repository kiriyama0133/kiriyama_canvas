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
            const RectD frame = frameAt({tick.position, layout.plot.bottom() + gap}, metrics,
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
            const RectD frame = frameAt({layout.plot.left() - gap, tick.position}, metrics,
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
            const RectD frame = frameAt({layout.plot.right() + gap, tick.position}, metrics,
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
        const RectD frame = frameAt({layout.plot.center().x, y}, metrics, TextAlign::Center,
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
        const RectD frame = frameAt(anchor, metrics, TextAlign::Center, baseline);
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
    const RectD frame = frameAt(topLeft, metrics, TextAlign::Left, TextBaseline::Top);
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
    const RectD frame = frameAt(center, metrics, TextAlign::Center, TextBaseline::Middle);
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
    result.issues = countIssues(dataset);

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
        wantsSecondary ? boundsOfSeries(dataset, dataset.series.size() - secondaryCount,
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

SceneBuildResult buildPieChart(const PieChartData& data, Vec2d canvasSize,
                               const SceneBuildOptions& options) {
    SceneBuildResult result;
    result.issues = countIssues(data);

    canvasSize = {std::max(0.0, canvasSize.x), std::max(0.0, canvasSize.y)};

    LayoutOptions measureOptions;
    measureOptions.measureText = options.textMeasure;

    PieLayoutOptions pieOptions = options.pie;
    if (options.showChartTitle && !options.title.empty()) {
        pieOptions.padding.top +=
            measureText(measureOptions, options.title, options.titleFontSize).height +
            options.titleGap;
    }

    result.pie = computePieLayout(canvasSize, data, pieOptions);
    const PieLayout& layout = result.pie;

    const BuildContext ctx{canvasSize, measureOptions, options};

    auto root = makeGroup("chart", "chart");
    root->appendChild(makeRect("background", {0.0, 0.0, canvasSize.x, canvasSize.y},
                               ColorToken::Background));

    if (!layout.valid) {
        root->appendChild(buildEmptyText(ctx));
        result.root = std::move(root);
        result.valid = false;
        return result;
    }

    auto sectors = makeGroup("slices", "slices");
    // 缝隙描边：调用方给了直接色就用之，否则用背景色 token"擦"出缝（跟主题走）。
    const Paint gapPaint =
        options.pieGapColor.visible() ? Paint{options.pieGapColor} : Paint{ColorToken::Background};
    // 标签文字：组件给了覆盖色用直接色，否则跟随主题。
    const Paint labelPaint = options.pieLabelColorOverride.visible()
                                 ? Paint{options.pieLabelColorOverride}
                                 : Paint{ColorToken::Label};
    for (const PieSector& sector : layout.sectors) {
        const std::size_t index = sector.sliceIndex;
        std::vector<Vec2d> points = sectorPolygon(sector, layout, options.pieSegments);
        sectors->appendChild(makePolygon("slice." + std::to_string(index), std::move(points),
                                         Paint::series(index), gapPaint,
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

        const TextMetrics metrics = measureText(measureOptions, text, options.labelFontSize);
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
            clampToCanvas(frameAt(anchor, metrics, align, TextBaseline::Middle), layout.canvas);
        sectors->appendChild(makeText("slice.label." + std::to_string(index), text, frame,
                                      options.labelFontSize, labelPaint, align,
                                      TextBaseline::Middle));
    }
    root->appendChild(std::move(sectors));

    if (options.showChartTitle && !options.title.empty()) {
        root->appendChild(buildChartTitle(ctx, {pieOptions.padding.left,
                                                options.pie.padding.top}));
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
