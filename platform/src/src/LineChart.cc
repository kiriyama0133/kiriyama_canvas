#include "kiriyama/scene/LineChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "kiriyama/core/SceneDiff.h"

namespace kiriyama::scene {

using core::AxisLayout;
using core::ColorToken;
using core::CursorShape;
using core::DataPoint;
using core::NodeType;
using core::Paint;
using core::RectD;
using core::SceneBuildOptions;
using core::SceneBuildResult;
using core::SceneNode;
using core::SceneStyle;
using core::Series;
using core::TickMark;
using core::Vec2d;

namespace {

[[nodiscard]] std::string formatFallbackNumber(double value) {
    if (!std::isfinite(value)) {
        return {};
    }
    char buffer[64] = {};
    if (std::abs(value - std::round(value)) <= 1e-9) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(std::llround(value)));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    }
    return buffer;
}

[[nodiscard]] Paint withAlphaScale(Paint base, float alphaScale) noexcept {
    base.alphaScale =
        static_cast<float>(math::saturate(static_cast<double>(base.alphaScale) * alphaScale));
    return base;
}

}  // namespace

// ================================================================ LinePointNode

LinePointNode::LinePointNode(std::string key) : TooltipTarget(std::move(key)) {
    setStopPointerPropagation(true);
    mutableStyle().cursor = CursorShape::Pointer;

    auto halo = std::make_unique<SceneNode>(NodeType::Ellipse, "halo");
    halo_ = halo.get();
    appendChild(std::move(halo));

    auto dot = std::make_unique<SceneNode>(NodeType::Ellipse, "dot");
    dot_ = dot.get();
    appendChild(std::move(dot));

    syncVisual();
}

void LinePointNode::setPointPosition(const Vec2d& center) noexcept {
    pointPosition_ = center;
    setTranslation(center);
    const double r = maxInteractiveRadius();
    setFrame({-r, -r, r * 2.0, r * 2.0});
}

void LinePointNode::setPointData(std::size_t seriesIndex, std::size_t pointIndex,
                                 const Vec2d& dataValue, std::string seriesName,
                                 std::string xLabel, std::string yLabel) {
    seriesIndex_ = seriesIndex;
    pointIndex_ = pointIndex;
    dataValue_ = dataValue;
    seriesName_ = std::move(seriesName);
    xLabel_ = std::move(xLabel);
    yLabel_ = std::move(yLabel);
    visualDirty_ = true;
}

void LinePointNode::setPointStyle(const LinePointStyle& style) {
    style_ = style;
    const double r = maxInteractiveRadius();
    setFrame({-r, -r, r * 2.0, r * 2.0});
    visualDirty_ = true;
    syncVisual();
}

double LinePointNode::maxInteractiveRadius() const noexcept {
    return std::max({1.0, style_.radius, style_.hoverRadius, style_.hitRadius});
}

bool LinePointNode::hitShape(const Vec2d& worldPoint) const noexcept {
    const Vec2d local = toLocal(worldPoint);
    const double r = std::max(style_.radius, style_.hitRadius);
    return (local.x * local.x + local.y * local.y) <= r * r;
}

CursorShape LinePointNode::cursorAt(const Vec2d& worldPoint) const noexcept {
    return hitTest(worldPoint) ? CursorShape::Pointer : CursorShape::Default;
}

void LinePointNode::onPointerEnter() {
    visualDirty_ = true;
    syncVisual();
}

void LinePointNode::onPointerLeave() {
    visualDirty_ = true;
    syncVisual();
}

void LinePointNode::onUpdate(const FrameContext&) {
    const bool isHovered = hovered();
    if (visualDirty_ || isHovered != lastHovered_) {
        syncVisual();
    }
}

void LinePointNode::syncVisual() {
    visualDirty_ = false;
    const bool isHovered = hovered();
    lastHovered_ = isHovered;

    const Paint fillPaint =
        style_.fill.visible() ? style_.fill : Paint::series(seriesIndex_);

    if (halo_ != nullptr) {
        const double haloR = std::max(0.0, style_.haloRadius);
        const bool showHalo = isHovered && haloR > 0.0 && style_.haloOpacity > 0.0f;
        halo_->setVisible(showHalo);
        halo_->setFrame({-haloR, -haloR, haloR * 2.0, haloR * 2.0});
        SceneStyle haloStyle;
        haloStyle.fill = withAlphaScale(fillPaint, style_.haloOpacity);
        halo_->setStyle(haloStyle);
    }

    if (dot_ != nullptr) {
        const double dotR = std::max(0.0, isHovered ? style_.hoverRadius : style_.radius);
        const bool showDot = (isHovered || style_.showIdle) && dotR > 0.0;
        dot_->setVisible(showDot);
        dot_->setFrame({-dotR, -dotR, dotR * 2.0, dotR * 2.0});
        SceneStyle dotStyle;
        dotStyle.fill = fillPaint;
        dotStyle.stroke = style_.stroke;
        dotStyle.strokeWidth = std::max(0.0, style_.strokeWidth);
        dot_->setStyle(dotStyle);
    }
}

TooltipText LinePointNode::tooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return TooltipText{tipTitle_, tipBody_};
    }

    std::string title;
    if (!xLabel_.empty() && !seriesName_.empty()) {
        title = xLabel_ + " · " + seriesName_;
    } else if (!xLabel_.empty()) {
        title = xLabel_;
    } else if (!seriesName_.empty()) {
        title = seriesName_;
    } else {
        title = formatFallbackNumber(dataValue_.x);
    }

    std::string body = !yLabel_.empty() ? yLabel_ : formatFallbackNumber(dataValue_.y);
    return TooltipText{std::move(title), std::move(body)};
}

bool LinePointNode::hasTooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return true;
    }
    return !yLabel_.empty() || std::isfinite(dataValue_.y);
}

RectD LinePointNode::tooltipAnchor() const {
    const Vec2d worldCenter = toWorld({0.0, 0.0});
    const double r = std::max(1.0, hovered() ? style_.hoverRadius : style_.radius);
    return {worldCenter.x - r, worldCenter.y - r, r * 2.0, r * 2.0};
}

// ================================================================ LineChart

LineChart::LineChart(std::string key) : Widget(std::move(key)) {}

void LineChart::setData(core::Dataset data) {
    data_ = std::move(data);
    dataDirty_ = true;
}

void LineChart::clearData() {
    data_ = {};
    dataDirty_ = true;
}

void LineChart::setConfig(const LineChartConfig& config) {
    config_ = config;
    dataDirty_ = true;
}

void LineChart::setXAxis(const core::AxisOptions& xAxis) {
    config_.layout.x = xAxis;
    dataDirty_ = true;
}

void LineChart::setYAxis(const core::AxisOptions& yAxis) {
    config_.layout.y = yAxis;
    dataDirty_ = true;
}

void LineChart::setPointStyle(const LinePointStyle& style) {
    config_.pointStyle = style;
    for (LinePointNode* node : pointNodes_) {
        if (node != nullptr) {
            node->setPointStyle(style);
        }
    }
}

void LineChart::setPalette(std::vector<Color> palette) {
    palette_ = std::move(palette);
    paletteShared_ =
        palette_.empty() ? nullptr : std::make_shared<const std::vector<Color>>(palette_);
    mutableStyle().seriesPalette = paletteShared_;
    dataDirty_ = true;
}

void LineChart::setTextMeasure(core::TextMeasureFn measure) {
    config_.textMeasure = std::move(measure);
    config_.layout.measureText = config_.textMeasure;
    dataDirty_ = true;
}

const LinePointNode* LineChart::hoveredPointNode() const noexcept {
    for (const LinePointNode* node : pointNodes_) {
        if (node != nullptr && node->hovered()) {
            return node;
        }
    }
    return nullptr;
}

void LineChart::onUpdate(const FrameContext&) {
    if (frame().x != 0.0 || frame().y != 0.0) {
        setTranslation(frame().origin());
        setFrame({0.0, 0.0, frame().width, frame().height});
    }
    if (dataDirty_ || !(frame().size() == lastSize_)) {
        rebuild();
    }
}

void LineChart::rebuild() {
    dataDirty_ = false;
    lastSize_ = frame().size();

    if (sceneGroup_ == nullptr) {
        auto sceneGroup = std::make_unique<SceneNode>(NodeType::Group, "line.scene");
        sceneGroup_ = sceneGroup.get();
        appendChild(std::move(sceneGroup));
    }
    if (pointsGroup_ == nullptr) {
        auto pointsGroup = std::make_unique<SceneNode>(NodeType::Group, "line.points");
        pointsGroup_ = pointsGroup.get();
        appendChild(std::move(pointsGroup));
    }

    core::LayoutOptions layoutOpts = config_.layout;
    if (!layoutOpts.measureText && config_.textMeasure) {
        layoutOpts.measureText = config_.textMeasure;
    }

    SceneBuildOptions buildOpts;
    buildOpts.curve = config_.curve;
    buildOpts.lineWidth = config_.lineWidth;
    buildOpts.curveSegments = config_.curveSegments;
    // 数据节点统一由交互式 LinePointNode 子组件承载，不在静态描述树里重复画圆点。
    buildOpts.showPoints = false;
    buildOpts.pointRadius = 0.0;
    buildOpts.showArea = config_.showArea;
    buildOpts.areaOpacity = config_.areaOpacity;
    buildOpts.showGrid = config_.showGrid;
    buildOpts.showCategoryGrid = config_.showCategoryGrid;
    buildOpts.showPlotBackground = config_.showPlotBackground;
    buildOpts.showAxisLines = config_.showAxisLines;
    buildOpts.showAxisLabels = config_.showAxisLabels;
    buildOpts.showAxisTitles = config_.showAxisTitles;
    buildOpts.title = config_.title;
    buildOpts.titleFontSize = config_.titleFontSize;
    buildOpts.titleGap = config_.titleGap;
    buildOpts.secondarySeriesCount = config_.secondarySeriesCount;
    buildOpts.textMeasure = layoutOpts.measureText;

    SceneBuildResult built = core::buildLineChart(data_, lastSize_, layoutOpts, buildOpts);
    layout_ = std::move(built.layout);
    valid_ = built.valid;

    if (built.root != nullptr) {
        (void)core::reconcileScene(*sceneGroup_, *built.root);
    } else {
        sceneGroup_->clearChildren();
    }

    syncPointNodes();
}

std::string LineChart::resolveXTickText(std::size_t pointIndex, double xValue) const {
    const auto& xTicks = config_.layout.x.tickLabels;
    if (pointIndex < xTicks.size() && !xTicks[pointIndex].empty()) {
        return xTicks[pointIndex];
    }
    if (pointIndex < data_.categories.size() && !data_.categories[pointIndex].empty()) {
        return data_.categories[pointIndex];
    }
    for (const TickMark& mark : layout_.xAxis.ticks) {
        if (std::abs(mark.value - xValue) <= 1e-6 && !mark.label.empty()) {
            return mark.label;
        }
    }
    const double step = layout_.xAxis.ticks.size() > 1
                            ? std::abs(layout_.xAxis.ticks[1].value - layout_.xAxis.ticks[0].value)
                            : 1.0;
    return config_.layout.x.format.format(xValue, step);
}

std::string LineChart::resolveYTickText(double yValue, bool useSecondaryY) const {
    const AxisLayout& axis = useSecondaryY ? layout_.secondaryYAxis : layout_.yAxis;
    const core::AxisOptions& axisOpt =
        useSecondaryY ? config_.layout.secondaryY : config_.layout.y;
    for (const TickMark& mark : axis.ticks) {
        if (std::abs(mark.value - yValue) <= 1e-6 && !mark.label.empty()) {
            return mark.label;
        }
    }
    const double step = axis.ticks.size() > 1
                            ? std::abs(axis.ticks[1].value - axis.ticks[0].value)
                            : 1.0;
    return axisOpt.format.format(yValue, step);
}

void LineChart::syncPointNodes() {
    if (pointsGroup_ == nullptr) {
        return;
    }
    if (!valid_) {
        pointsGroup_->clearChildren();
        pointNodes_.clear();
        return;
    }

    const std::size_t secondaryFirst =
        (config_.secondarySeriesCount > 0 &&
         config_.secondarySeriesCount < data_.series.size())
            ? data_.series.size() - config_.secondarySeriesCount
            : data_.series.size();

    std::size_t cursor = 0;
    for (std::size_t s = 0; s < data_.series.size(); ++s) {
        const Series& series = data_.series[s];
        const bool useSecondaryY = s >= secondaryFirst;
        for (std::size_t j = 0; j < series.points.size(); ++j) {
            const DataPoint& pt = series.points[j];
            if (!pt.valid()) {
                continue;
            }

            const std::string key =
                "point." + std::to_string(s) + "." + std::to_string(j);
            LinePointNode* node = nullptr;
            if (cursor < pointNodes_.size()) {
                node = pointNodes_[cursor];
                node->setKey(key);
            } else {
                auto created = std::make_unique<LinePointNode>(key);
                node = created.get();
                pointsGroup_->appendChild(std::move(created));
                pointNodes_.push_back(node);
            }

            const Vec2d pixel = useSecondaryY ? layout_.toPixelSecondaryY(pt.x, pt.y)
                                              : layout_.toPixel(pt.x, pt.y);
            node->setPointData(s, j, {pt.x, pt.y}, series.name,
                               resolveXTickText(j, pt.x),
                               resolveYTickText(pt.y, useSecondaryY));
            node->setPointStyle(config_.pointStyle);
            node->setPointPosition(pixel);
            ++cursor;
        }
    }

    while (pointNodes_.size() > cursor) {
        LinePointNode* tail = pointNodes_.back();
        pointNodes_.pop_back();
        (void)pointsGroup_->removeChild(tail);
    }
}

}  // namespace kiriyama::scene
