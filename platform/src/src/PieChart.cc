#include "kiriyama/scene/PieChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "kiriyama/core/SceneDiff.h"

namespace kiriyama::scene {

using core::Color;
using core::CursorShape;
using core::HitResult;
using core::kInvalidIndex;
using core::NodeType;
using core::PieLayout;
using core::PieSlicesBuildResult;
using core::RectD;
using core::SceneNode;
using core::Vec2d;

namespace {

constexpr double kExplodeDuration = 0.12;

}  // namespace

PieChart::PieChart(std::string key) : TooltipTarget(std::move(key)) {}

// ---------------------------------------------------------------- 数据

void PieChart::setData(const core::PieChartData& data) {
    data_ = data;
    dataDirty_ = true;
}

void PieChart::clearData() {
    data_ = {};
    dataDirty_ = true;
}

// ---------------------------------------------------------------- 样式 / 布局

void PieChart::setStyle(const PieStyle& style) {
    style_ = style;
    dataDirty_ = true;
}

void PieChart::setLayoutOptions(const core::PieLayoutOptions& options) {
    style_.layout = options;
    dataDirty_ = true;
}

bool PieChart::valid() const noexcept {
    return layout_.valid && sliceCount() > 0;
}

void PieChart::setPalette(std::vector<Color> palette) {
    palette_ = std::move(palette);
    paletteShared_ =
        palette_.empty() ? nullptr : std::make_shared<const std::vector<Color>>(palette_);
    dataDirty_ = true;
}

void PieChart::setSegments(int segments) noexcept {
    style_.segments = segments;
    dataDirty_ = true;
}

void PieChart::setSliceDefaults(double explodeDistance) noexcept {
    style_.explodeDistance = std::max(0.0, explodeDistance);
    dataDirty_ = true;
}

void PieChart::setLabelStyle(bool showLabels, bool showValues, double fontSize, Color color) {
    style_.showLabels = showLabels;
    style_.showValues = showValues;
    style_.labelFontSize = fontSize;
    style_.labelColor = color;
    dataDirty_ = true;
}

void PieChart::setGapStyle(double width, Color color) {
    style_.gapWidth = std::max(0.0, width);
    style_.gapColor = color;
    dataDirty_ = true;
}

double PieChart::explodeRadius(std::size_t index) const noexcept {
    if (index >= explodeProgress_.size()) {
        return 0.0;
    }
    return style_.explodeDistance * explodeProgress_[index];
}

// ---------------------------------------------------------------- 重建与局部快路径（全部委托给 core::SceneBuilder）

void PieChart::rebuild() {
    dataDirty_ = false;
    lastSize_ = frame().size();

    if (inner_ == nullptr) {
        auto inner = std::make_unique<SceneNode>(NodeType::Group, "slices");
        inner_ = inner.get();
        appendChild(std::move(inner));
    }

    PieSlicesBuildResult built =
        core::buildPieSlices(data_, lastSize_, style_, paletteShared_);
    layout_ = std::move(built.layout);

    explodeProgress_.assign(layout_.sectors.size(), 0.0);
    hoveredIndex_ = kInvalidIndex;

    if (!built.valid || !built.root) {
        inner_->clearChildren();
        return;
    }

    (void)core::reconcileScene(*inner_, *built.root);
}

// ---------------------------------------------------------------- 每帧驱动

void PieChart::onUpdate(const FrameContext& ctx) {
    if (frame().x != 0.0 || frame().y != 0.0) {
        setTranslation(frame().origin());
        setFrame({0.0, 0.0, frame().width, frame().height});
    }
    if (dataDirty_ || !(frame().size() == lastSize_)) {
        rebuild();
    }
    updateHoverAndExplode(ctx);
}

std::size_t PieChart::pickSector(const Vec2d& worldPoint) const noexcept {
    if (!layout_.valid || data_.slices.empty()) {
        return kInvalidIndex;
    }
    const Vec2d local = toLocal(worldPoint) - frame().origin();
    HitResult hit = core::hitTestPie(data_, layout_, local);
    if (!hit.hit) {
        const double grown = hoveredIndex_ < explodeProgress_.size()
                                 ? style_.explodeDistance * explodeProgress_[hoveredIndex_]
                                 : 0.0;
        if (grown > 0.0) {
            PieLayout expanded = layout_;
            expanded.outerRadius += grown;
            const HitResult expandedHit = core::hitTestPie(data_, expanded, local);
            if (expandedHit.hit && expandedHit.sliceIndex == hoveredIndex_) {
                hit = expandedHit;
            }
        }
    }
    if (hit.hit && hit.sliceIndex < layout_.sectors.size()) {
        return hit.sliceIndex;
    }
    return kInvalidIndex;
}

void PieChart::updateHoverAndExplode(const FrameContext& ctx) {
    hoveredIndex_ = pointerInside() ? pickSector(ctx.pointer.position) : kInvalidIndex;

    if (explodeProgress_.size() != layout_.sectors.size() || inner_ == nullptr) {
        return;
    }
    const double step = ctx.deltaTime / kExplodeDuration;
    for (std::size_t i = 0; i < explodeProgress_.size(); ++i) {
        const double target = i == hoveredIndex_ ? 1.0 : 0.0;
        double& progress = explodeProgress_[i];
        if (progress == target) {
            continue;
        }
        progress = progress < target ? std::min(target, progress + step)
                                     : std::max(target, progress - step);
        core::updatePieSliceExplode(*inner_, layout_, i, style_.explodeDistance * progress,
                                    style_.segments);
    }
}

// ---------------------------------------------------------------- 命中 / 光标 / 气泡

bool PieChart::hitShape(const Vec2d& worldPoint) const noexcept {
    return pickSector(worldPoint) != kInvalidIndex;
}

CursorShape PieChart::cursorAt(const Vec2d& worldPoint) const noexcept {
    return hitTest(worldPoint) ? CursorShape::Pointer : CursorShape::Default;
}

TooltipText PieChart::tooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return TooltipText{tipTitle_, tipBody_};
    }
    if (hoveredIndex_ == kInvalidIndex || hoveredIndex_ >= layout_.sectors.size()) {
        return {};
    }

    const core::PieSector& sector = layout_.sectors[hoveredIndex_];
    const char* label = "";
    double value = sector.value;
    if (hoveredIndex_ < data_.slices.size()) {
        label = data_.slices[hoveredIndex_].label.c_str();
        value = data_.slices[hoveredIndex_].value;
    }

    char body[96] = {};
    if (std::isfinite(value)) {
        if (value == static_cast<double>(static_cast<long long>(value))) {
            std::snprintf(body, sizeof(body), "%lld · %.1f%%",
                          static_cast<long long>(value), sector.fraction * 100.0);
        } else {
            std::snprintf(body, sizeof(body), "%.2f · %.1f%%", value, sector.fraction * 100.0);
        }
    }
    return TooltipText{label, body};
}

bool PieChart::hasTooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return true;
    }
    return hoveredIndex_ != kInvalidIndex;
}

RectD PieChart::tooltipAnchor() const {
    return hitBounds();
}

}  // namespace kiriyama::scene
