#include "kiriyama/scene/PieChart.h"

#include "kiriyama/core/SceneDiff.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace kiriyama::scene {

using core::Color;
using core::HitResult;
using core::kInvalidIndex;
using core::NodeType;
using core::PieLayout;
using core::RectD;
using core::SceneBuildResult;
using core::SceneNode;
using core::Vec2d;

namespace {

/// 外扩进/出的时长（秒）。进出共用，进度线性趋近目标。
constexpr double kExplodeDuration = 0.12;

} // namespace

PieChart::PieChart(std::string key) : TooltipTarget(std::move(key)) {}

// ---------------------------------------------------------------- 数据

void PieChart::setData(const core::PieChartData& data) {
    data_      = data;
    dataDirty_ = true;
}

void PieChart::clearData() {
    data_      = {};
    dataDirty_ = true;
}

// ---------------------------------------------------------------- 布局 / 外观

void PieChart::setLayoutOptions(const core::PieLayoutOptions& options) {
    options_   = options;
    dataDirty_ = true;
}

bool PieChart::valid() const noexcept {
    return layout_.valid && sliceCount() > 0;
}

void PieChart::setPalette(std::vector<Color> palette) {
    palette_       = std::move(palette);
    paletteShared_ = palette_.empty() ? nullptr : std::make_shared<const std::vector<Color>>(palette_);
    dataDirty_     = true;
}

void PieChart::setSegments(int segments) noexcept {
    segments_  = segments;
    dataDirty_ = true;
}

void PieChart::setSliceDefaults(double explodeDistance) noexcept {
    explodeDistance_ = explodeDistance < 0.0 ? 0.0 : explodeDistance;
    dataDirty_       = true;
}

void PieChart::setLabelStyle(bool showLabels, bool showValues, double fontSize, Color color) {
    showLabels_    = showLabels;
    showValues_    = showValues;
    labelFontSize_ = fontSize;
    labelColor_    = color;
    dataDirty_     = true;
}

void PieChart::setGapStyle(double width, Color color) {
    gapWidth_  = width < 0.0 ? 0.0 : width;
    gapColor_  = color;
    dataDirty_ = true;
}

double PieChart::explodeRadius(std::size_t index) const noexcept {
    if (index >= explodeProgress_.size()) {
        return 0.0;
    }
    return explodeDistance_ * explodeProgress_[index];
}

// ---------------------------------------------------------------- 重建（描述树 diff）

core::SceneBuildOptions PieChart::buildOptions() const {
    core::SceneBuildOptions build;
    // 颜色一律语义 token：扇区 = Token::Series（实例色板挂在对账根组上，
    // 渲染时沿树继承解析），标签 = Token::Label；组件级覆盖色走 override。
    // 主题在渲染层统一解析——切深浅色不用动组件。
    build.labelFontSize         = labelFontSize_;
    build.pieLabelColorOverride = labelColor_;
    build.showPieLabels         = showLabels_;
    build.showPieValues         = showValues_;
    build.pieSegments           = segments_;
    build.pieSliceGap           = gapWidth_;
    build.pieGapColor           = gapColor_;
    build.showEmptyText         = false; // 空数据画空组（保持"组件不画提示"的旧约定）
    build.pie                   = options_;
    build.textMeasure           = textMeasure_;
    return build;
}

void PieChart::collectSliceNodes() {
    sliceNodes_.clear();
    if (inner_ == nullptr) {
        return;
    }
    sliceNodes_.reserve(inner_->childCount());
    for (const auto& child : inner_->children()) {
        // key 约定见 SceneBuilder：slice.<i> 是扇区多边形，slice.label.<i> 是文本。
        if (child->key().rfind("slice.", 0) == 0 && child->type() == NodeType::Polygon) {
            sliceNodes_.push_back(child.get());
        }
    }
}

void PieChart::rebuild() {
    dataDirty_ = false;
    lastSize_  = frame().size();

    // 组件自身持 transform_ 承担位置平移，inner_ 直接挂在组件下作为对账目标。
    if (inner_ == nullptr) {
        auto inner = std::make_unique<SceneNode>(NodeType::Group, "slices");
        inner_     = inner.get();
        appendChild(std::move(inner));
    }

    // buildPieChart 内部已调用 computePieLayout，直接复用其返回的 described.pie，
    SceneBuildResult described = core::buildPieChart(data_, lastSize_, buildOptions());
    layout_                    = std::move(described.pie);

    // 进度向量对齐扇区数。新数据清零：不继承旧片的动画（沿用旧约定）。
    explodeProgress_.assign(layout_.sectors.size(), 0.0);
    hoveredIndex_ = kInvalidIndex;

    if (!layout_.valid || data_.slices.empty()) {
        inner_->clearChildren();
        sliceNodes_.clear();
        return;
    }

    // 只要 "slices" 组：背景 / 标题 / 空状态不归组件管（透明画在页面画布上）。
    SceneNode* slices = nullptr;
    if (described.root != nullptr) {
        for (const auto& child : described.root->children()) {
            if (child->key() == "slices") {
                slices = child.get();
                break;
            }
        }
    }
    if (slices == nullptr) {
        inner_->clearChildren();
        sliceNodes_.clear();
        return;
    }

    // 实例系列色板挂在对账根组上：扇区 fill 是 Token::Series，
    // 渲染时沿树继承到这里解析。paletteShared_ 缓住指针，色板未变时 diff 零写入。
    slices->mutableStyle().seriesPalette = paletteShared_;

    // 就地对账：key 相同的扇区节点保身份，只写变化的属性。
    (void)core::reconcileScene(*inner_, *slices);
    collectSliceNodes();

    // 已在外扩中的扇区把放大后的几何写回（对账写的是基准几何）。
    for (std::size_t i = 0; i < sliceNodes_.size() && i < explodeProgress_.size(); ++i) {
        if (explodeProgress_[i] > 0.0) {
            applyExplode(i, explodeDistance_ * explodeProgress_[i]);
        }
    }
}

// ---------------------------------------------------------------- 每帧

void PieChart::onUpdate(const FrameContext& ctx) {
    // 若调用方通过 setFrame({x, y, w, h}) 传入了偏移，将其收敛到自身 transform_，
    // 让 frame_ 始终保持局部尺寸 {0, 0, w, h}；单纯移动位置不再触发 rebuild。
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
    HitResult   hit   = core::hitTestPie(data_, layout_, local);
    if (!hit.hit) {
        // 外扩中的扇区比基准几何多出一圈可点区域，仅对该外扩扇区生效。
        const double grown =
            hoveredIndex_ < explodeProgress_.size() ? explodeDistance_ * explodeProgress_[hoveredIndex_] : 0.0;
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
    hoveredIndex_ = pickSector(ctx.pointer.position);

    // ---- 外扩动画：进度逐扇区趋近 hovered?1:0；变化中的扇区原地写几何。
    if (explodeProgress_.size() != layout_.sectors.size()) {
        return; // rebuild 还没跑（首帧 / 数据刚清空），下一帧再推进
    }
    const double step = ctx.deltaTime / kExplodeDuration;
    for (std::size_t i = 0; i < explodeProgress_.size(); ++i) {
        const double target   = i == hoveredIndex_ ? 1.0 : 0.0;
        double&      progress = explodeProgress_[i];
        if (progress == target) {
            continue;
        }
        progress = progress < target ? std::min(target, progress + step) : std::max(target, progress - step);
        applyExplode(i, explodeDistance_ * progress);
    }
}

void PieChart::applyExplode(std::size_t index, double radius) {
    if (index >= sliceNodes_.size() || index >= layout_.sectors.size()) {
        return;
    }
    core::SceneNode* node = sliceNodes_[index];
    if (node == nullptr) {
        return;
    }
    // 外扩 = 外半径放大（圆心与角度边界不动），不是平移——平移会在相邻
    // 扇区边上露出底色。sectorPolygon 直接给出放大后的顶点。
    PieLayout expanded = layout_;
    expanded.outerRadius += radius;
    node->setPoints(core::sectorPolygon(layout_.sectors[index], expanded, segments_));
}

// ---------------------------------------------------------------- 命中 / 气泡

bool PieChart::hitShape(const Vec2d& worldPoint) const noexcept {
    return pickSector(worldPoint) != kInvalidIndex;
}

TooltipText PieChart::tooltipContent() const {
    // 静态内容优先（TooltipTarget 的入口一），没有才按悬停扇区生成。
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return TooltipText{tipTitle_, tipBody_};
    }
    if (hoveredIndex_ == kInvalidIndex || hoveredIndex_ >= layout_.sectors.size()) {
        return {};
    }

    const core::PieSector& sector = layout_.sectors[hoveredIndex_];
    const char*            label  = "";
    double                 value  = sector.value;
    if (hoveredIndex_ < data_.slices.size()) {
        label = data_.slices[hoveredIndex_].label.c_str();
        value = data_.slices[hoveredIndex_].value;
    }

    char body[96] = {};
    if (std::isfinite(value)) {
        if (value == static_cast<double>(static_cast<long long>(value))) {
            std::snprintf(body, sizeof(body), "%lld · %.1f%%", static_cast<long long>(value), sector.fraction * 100.0);
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
    // 气泡锚在整张饼图的世界包围盒（frame 经 transform_ 平移后的区域）上：
    // hover 哪一片气泡都浮在图表上方，不会压在扇区上、也不会在相邻扇区之间跳来跳去。
    return hitBounds();
}

} // namespace kiriyama::scene
