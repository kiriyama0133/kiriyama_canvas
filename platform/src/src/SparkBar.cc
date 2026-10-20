#include "kiriyama/scene/SparkBar.h"

#include <cstdio>
#include <memory>
#include <utility>

#include "kiriyama/core/SceneBuilder.h"
#include "kiriyama/core/SceneDiff.h"

namespace kiriyama::scene {

namespace {

using core::Color;
using core::NodeType;
using core::RectD;
using core::SceneNode;
using core::SparkBarPick;
using core::Vec2d;

[[nodiscard]] std::string formatValue(double value) {
    char buffer[48] = {};
    if (value == static_cast<double>(static_cast<long long>(value))) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    }
    return buffer;
}

}  // namespace

SparkBar::SparkBar(std::string key) : TooltipTarget(std::move(key)) {
    colors_ = core::defaultSeriesPalette();
}

// ---------------------------------------------------------------- 数据 / 样式

void SparkBar::setData(core::SparkBarData data) {
    data_ = std::move(data);
    dataDirty_ = true;
}

void SparkBar::setOptions(const core::SparkBarOptions& options) {
    options_ = options;
    dataDirty_ = true;
}

void SparkBar::setColors(std::vector<core::Color> colors) {
    colors_ = colors.empty() ? core::defaultSeriesPalette() : std::move(colors);
    dataDirty_ = true;
}

core::Color SparkBar::colorAt(std::size_t seriesIndex) const noexcept {
    if (colors_.empty()) {
        return core::Color::Hex(0x85B7EB);
    }
    const core::Color candidate = colors_[seriesIndex % colors_.size()];
    if (candidate.visible()) {
        return candidate;
    }
    const auto& fallback = core::defaultSeriesPalette();
    return fallback.empty() ? core::Color::Hex(0x85B7EB)
                            : fallback[seriesIndex % fallback.size()];
}

// ---------------------------------------------------------------- 命中（委托给 core::hitTestSparkBar）

bool SparkBar::hitShape(const Vec2d& worldPoint) const noexcept {
    return core::hitTestSparkBar(layout_, toLocal(worldPoint) - frame().origin()).hit();
}

core::CursorShape SparkBar::cursorAt(const Vec2d& worldPoint) const noexcept {
    return hitTest(worldPoint) ? core::CursorShape::Pointer : core::CursorShape::Default;
}

// ---------------------------------------------------------------- 气泡

bool SparkBar::hasTooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return true;
    }
    return layout_.valid && data_.tagCount() > 0;
}

TooltipText SparkBar::tooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return TooltipText{tipTitle_, tipBody_};
    }
    if (data_.tags.empty() && data_.series.empty()) {
        return {};
    }

    const SparkBarPick pick =
        (hoverItem_ != core::kInvalidIndex || hoverLegendSeries_ != core::kInvalidIndex)
            ? SparkBarPick{hoverItem_, hoverSegment_, hoverLegendSeries_}
            : core::hitTestSparkBar(layout_, toLocal(pointerPosition()) - frame().origin());

    if (pick.legendSeries != core::kInvalidIndex && pick.legendSeries < data_.series.size()) {
        for (const core::SparkBarLegendItem& leg : layout_.legend) {
            if (leg.seriesIndex == pick.legendSeries) {
                char buffer[48] = {};
                std::snprintf(buffer, sizeof(buffer), " · %.1f%%", leg.fraction * 100.0);
                return TooltipText{data_.series[pick.legendSeries].name,
                                   formatValue(leg.value) + buffer};
            }
        }
    }

    if (pick.item == core::kInvalidIndex || pick.item >= layout_.items.size()) {
        return {};
    }

    const core::SparkBarItem& item = layout_.items[pick.item];
    if (item.tagIndex >= data_.tags.size()) {
        return {};
    }
    const std::string& tag = data_.tags[item.tagIndex];

    if (pick.segment != core::kInvalidIndex && pick.segment < item.segments.size()) {
        const core::SparkBarSegment& segment = item.segments[pick.segment];
        const double total = data_.totalAt(item.tagIndex);
        const std::size_t seriesIndex = segment.seriesIndex;
        const std::string name = seriesIndex < data_.series.size()
                                     ? data_.series[seriesIndex].name
                                     : std::string("Series ") + std::to_string(seriesIndex + 1);

        std::string body = formatValue(segment.value);
        if (total > 0.0) {
            char buffer[48] = {};
            std::snprintf(buffer, sizeof(buffer), " · %.1f%%", segment.value / total * 100.0);
            body += buffer;
        }
        return TooltipText{tag.empty() ? name : (tag + " · " + name), body};
    }

    std::string body;
    for (std::size_t s = 0; s < data_.seriesCount(); ++s) {
        const double value = data_.valueAt(item.tagIndex, s);
        if (!(value > 0.0)) {
            continue;
        }
        if (!body.empty()) {
            body += " · ";
        }
        body += data_.series[s].name;
        body += " ";
        body += formatValue(value);
    }
    const double total = data_.totalAt(item.tagIndex);
    if (total > 0.0) {
        if (!body.empty()) {
            body += " · ";
        }
        body += "合计 ";
        body += formatValue(total);
    }

    return TooltipText{tag, body};
}

core::RectD SparkBar::tooltipAnchor() const {
    return hitBounds();
}

// ---------------------------------------------------------------- 重建与局部更新（全部委托给 core）

void SparkBar::onUpdate(const FrameContext&) {
    if (frame().x != 0.0 || frame().y != 0.0) {
        setTranslation(frame().origin());
        setFrame({0.0, 0.0, frame().width, frame().height});
    }

    const bool needRebuild = dataDirty_ || !(frame().size() == lastSize_);
    if (needRebuild) {
        rebuild();
    }

    if (pointerInside()) {
        const SparkBarPick pick =
            core::hitTestSparkBar(layout_, toLocal(pointerPosition()) - frame().origin());
        hoverItem_ = pick.item;
        hoverSegment_ = pick.segment;
        hoverLegendSeries_ = pick.legendSeries;
    } else {
        hoverItem_ = core::kInvalidIndex;
        hoverSegment_ = core::kInvalidIndex;
        hoverLegendSeries_ = core::kInvalidIndex;
    }

    if (hoverItem_ != lastHoverItem_ || hoverSegment_ != lastHoverSegment_) {
        syncHover();
    }
}

void SparkBar::rebuild() {
    dataDirty_ = false;
    lastSize_ = frame().size();

    if (inner_ == nullptr) {
        auto inner = std::make_unique<SceneNode>(NodeType::Group, "sparkbar.scene");
        inner_ = inner.get();
        appendChild(std::move(inner));
    }

    core::SparkBarBuildResult described =
        core::buildSparkBar(data_, lastSize_, options_, colors_, hoverItem_, hoverSegment_);
    layout_ = std::move(described.layout);
    lastHoverItem_ = hoverItem_;
    lastHoverSegment_ = hoverSegment_;

    if (!described.valid || described.root == nullptr) {
        inner_->clearChildren();
        return;
    }
    (void)core::reconcileScene(*inner_, *described.root);
}

void SparkBar::syncHover() {
    lastHoverItem_ = hoverItem_;
    lastHoverSegment_ = hoverSegment_;
    if (inner_ != nullptr) {
        core::updateSparkBarHover(*inner_, layout_, options_, colors_, hoverItem_, hoverSegment_);
    }
}

}  // namespace kiriyama::scene
