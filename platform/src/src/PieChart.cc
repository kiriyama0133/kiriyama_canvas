#include "kiriyama/scene/PieChart.h"

#include <cmath>
#include <cstdio>
#include <utility>

#include "kiriyama/core/SceneDiff.h"

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

// ---------------------------------------------------------------- 布局 / 外观

void PieChart::setLayoutOptions(const core::PieLayoutOptions& options) {
    options_ = options;
    dataDirty_ = true;
}

bool PieChart::valid() const noexcept { return layout_.valid && sliceCount() > 0; }

void PieChart::setPalette(std::vector<Color> palette) {
    palette_ = std::move(palette);
    paletteShared_ = std::make_shared<const std::vector<Color>>(palette_);
    dataDirty_ = true;
}

void PieChart::setSegments(int segments) noexcept {
    segments_ = segments;
    dataDirty_ = true;
}

void PieChart::setSliceDefaults(double explodeDistance) noexcept {
    explodeDistance_ = explodeDistance < 0.0 ? 0.0 : explodeDistance;
    dataDirty_ = true;
}

void PieChart::setLabelStyle(bool showLabels, bool showValues, double fontSize, Color color) {
    showLabels_ = showLabels;
    showValues_ = showValues;
    labelFontSize_ = fontSize;
    labelColor_ = color;
    dataDirty_ = true;
}

void PieChart::setGapStyle(double width, Color color) {
    gapWidth_ = width < 0.0 ? 0.0 : width;
    gapColor_ = color;
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
    build.labelFontSize = labelFontSize_;
    build.pieLabelColorOverride = labelColor_;
    build.showPieLabels = showLabels_;
    build.showPieValues = showValues_;
    build.pieSegments = segments_;
    build.pieSliceGap = gapWidth_;
    build.pieGapColor = gapColor_;
    build.showEmptyText = false;  // 空数据画空组（保持"组件不画提示"的旧约定）
    build.pie = options_;
    build.textMeasure = textMeasure_;
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
    lastFrame_ = frame();

    layout_ = core::computePieLayout(frame().size(), data_, options_);

    // 两层结构：plot 组持 frame 平移（组件自己写，不参与对账），
    // inner 组是对账目标——reconcileNode 会把目标的 transform 一并同步成
    // 描述树的 identity，平移放在对账层之上才不会被抹掉。
    if (plot_ == nullptr) {
        auto plot = std::make_unique<SceneNode>(NodeType::Group, "plot");
        plot_ = plot.get();
        appendChild(std::move(plot));
    }
    plot_->setTranslation(frame().origin());

    if (inner_ == nullptr) {
        auto inner = std::make_unique<SceneNode>(NodeType::Group, "scene");
        inner_ = inner.get();
        plot_->appendChild(std::move(inner));
    }
    // 实例系列色板挂在对账根组上：扇区 fill 是 Token::Series，
    // 渲染时沿树继承到这里解析。palette 为空 = 用全局主题色板。
    if (!palette_.empty()) {
        core::SceneStyle style = inner_->style();
        style.seriesPalette =
            std::make_shared<const std::vector<Color>>(palette_);
        inner_->setStyle(style);
    } else if (inner_->style().seriesPalette != nullptr) {
        core::SceneStyle style = inner_->style();
        style.seriesPalette.reset();
        inner_->setStyle(style);
    }

    // 进度向量对齐扇区数。新数据清零：不继承旧片的动画（沿用旧约定）。
    explodeProgress_.assign(layout_.sectors.size(), 0.0);
    hoveredIndex_ = kInvalidIndex;

    if (!layout_.valid || data_.slices.empty()) {
        inner_->clearChildren();
        sliceNodes_.clear();
        return;
    }

    const SceneBuildResult described = core::buildPieChart(data_, frame().size(), buildOptions());

    // 只要 "slices" 组：背景 / 标题 / 空状态不归组件管（透明画在页面画布上）。
    const SceneNode* slices = nullptr;
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
    if (dataDirty_ || !(frame() == lastFrame_)) {
        rebuild();
    }
    updateHoverAndExplode(ctx);
}

void PieChart::updateHoverAndExplode(const FrameContext& ctx) {
    // ---- hover：基准布局先判一次；没中且正外扩时，用放大后的外半径再判一次
    //（外扩中的扇区比基准几何多出一圈可点区域，与旧 PieSlice::hitShape 一致）。
    std::size_t next = kInvalidIndex;
    if (layout_.valid && !data_.slices.empty()) {
        const Vec2d local = toLocal(ctx.pointer.position) - frame().origin();
        HitResult hit = core::hitTestPie(data_, layout_, local);
        if (!hit.hit) {
            const double grown = hoveredIndex_ != kInvalidIndex
                                     ? explodeDistance_ * explodeProgress_[hoveredIndex_]
                                     : 0.0;
            if (grown > 0.0) {
                PieLayout expanded = layout_;
                expanded.outerRadius += grown;
                hit = core::hitTestPie(data_, expanded, local);
            }
        }
        if (hit.hit && hit.sliceIndex < layout_.sectors.size()) {
            next = hit.sliceIndex;
        }
    }
    hoveredIndex_ = next;

    // ---- 外扩动画：进度逐扇区趋近 hovered?1:0；变化中的扇区原地写几何。
    if (explodeProgress_.size() != layout_.sectors.size()) {
        return;  // rebuild 还没跑（首帧 / 数据刚清空），下一帧再推进
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
    if (!layout_.valid || data_.slices.empty()) {
        return false;
    }
    const Vec2d local = toLocal(worldPoint) - frame().origin();
    if (core::hitTestPie(data_, layout_, local).hit) {
        return true;
    }
    // 外扩中的扇区多出一圈可点区域。
    const double grown = hoveredIndex_ != kInvalidIndex
                             ? explodeDistance_ * explodeProgress_[hoveredIndex_]
                             : 0.0;
    if (grown <= 0.0) {
        return false;
    }
    PieLayout expanded = layout_;
    expanded.outerRadius += grown;
    return core::hitTestPie(data_, expanded, local).hit;
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
    // 气泡锚在整张饼图的 frame 上：hover 哪一片气泡都浮在图表上方，
    // 不会压在扇区上、也不会在相邻扇区之间跳来跳去。
    if (!frame().empty()) {
        return frame();
    }
    return hitBounds();
}

}  // namespace kiriyama::scene
