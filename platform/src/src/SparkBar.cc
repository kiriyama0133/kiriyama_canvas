#include "kiriyama/scene/SparkBar.h"

#include <cstdio>
#include <memory>
#include <utility>

#include "kiriyama/core/SceneBuilder.h"  // defaultSeriesPalette
#include "kiriyama/core/SceneDiff.h"

namespace kiriyama::scene {

namespace {

using core::Color;
using core::NodeType;
using core::RectD;
using core::SceneNode;
using core::TextAlign;
using core::TextBaseline;
using core::Vec2d;

/// 数字格式化：整数不带小数点，小数两位。表格/卡片里的数值都这个口径。
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

void SparkBar::setData(const core::SparkBarData& data) {
    data_ = data;
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
    const auto fallback = core::defaultSeriesPalette();
    return fallback.empty() ? core::Color::Hex(0x85B7EB)
                            : fallback[seriesIndex % fallback.size()];
}

// ---------------------------------------------------------------- 命中

SparkBar::Pick SparkBar::pickAtLocal(const Vec2d& local) const noexcept {
    Pick pick;
    for (std::size_t i = 0; i < layout_.items.size(); ++i) {
        const core::SparkBarItem& item = layout_.items[i];
        const bool onBar = !item.barRect.empty() && item.barRect.contains(local);
        const bool onTag = !item.tagRect.empty() && item.tagRect.contains(local);
        if (!onBar && !onTag) {
            continue;
        }
        pick.item = i;
        // 段命中：横向条内部再细分。tag 行没有段。
        for (std::size_t s = 0; s < item.segments.size(); ++s) {
            if (item.segments[s].rect.contains(local)) {
                pick.segment = s;
                break;
            }
        }
        return pick;
    }
    // 底部图例项命中
    for (const core::SparkBarLegendItem& leg : layout_.legend) {
        if (!leg.hitRect.empty() && leg.hitRect.contains(local)) {
            pick.legendSeries = leg.seriesIndex;
            return pick;
        }
    }
    return pick;
}

bool SparkBar::hitShape(const Vec2d& worldPoint) const noexcept {
    const Pick pick = pickAtLocal(toLocal(worldPoint) - frame().origin());
    return pick.item != core::kInvalidIndex || pick.legendSeries != core::kInvalidIndex;
}

// ---------------------------------------------------------------- 气泡

bool SparkBar::hasTooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return true;
    }
    return layout_.valid && data_.tagCount() > 0;
}

TooltipText SparkBar::tooltipContent() const {
    // 静态内容优先（TooltipTarget 的入口一）。
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return TooltipText{tipTitle_, tipBody_};
    }
    if (data_.tags.empty() && data_.series.empty()) {
        return {};
    }

    // 优先复用 onUpdate 已经算好的缓存，避免每帧重复 pickAtLocal。
    const Pick pick =
        (hoverItem_ != core::kInvalidIndex || hoverLegendSeries_ != core::kInvalidIndex)
            ? Pick{hoverItem_, hoverSegment_, hoverLegendSeries_}
            : pickAtLocal(toLocal(pointerPosition()) - frame().origin());

    // 命中底部图例：展示该语言（series）在全部行中的汇总与总占比。
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

    // 落在某一段：报这一层的值 + 占整条比例。
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

    // 落在条上（段缝、或标题行）：整条汇总。
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
    // 气泡锚在整张 SparkBar 的世界包围盒上：hover 哪一段都浮在图上方，不压住条身。
    return hitBounds();
}

// ---------------------------------------------------------------- 重建与描述树对账

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
        const Pick pick = pickAtLocal(toLocal(pointerPosition()) - frame().origin());
        hoverItem_ = pick.item;
        hoverSegment_ = pick.segment;
        hoverLegendSeries_ = pick.legendSeries;
    } else {
        hoverItem_ = core::kInvalidIndex;
        hoverSegment_ = core::kInvalidIndex;
        hoverLegendSeries_ = core::kInvalidIndex;
    }

    if (needRebuild || hoverItem_ != lastHoverItem_ || hoverSegment_ != lastHoverSegment_) {
        syncScene();
    }
}

void SparkBar::rebuild() {
    dataDirty_ = false;
    lastSize_ = frame().size();
    layout_ = core::computeSparkBarLayout(lastSize_, data_, options_);
}

void SparkBar::syncScene() {
    lastHoverItem_ = hoverItem_;
    lastHoverSegment_ = hoverSegment_;

    if (inner_ == nullptr) {
        auto inner = std::make_unique<SceneNode>(NodeType::Group, "sparkbar.scene");
        inner_ = inner.get();
        appendChild(std::move(inner));
    }

    if (!layout_.valid) {
        inner_->clearChildren();
        return;
    }

    SceneNode described{NodeType::Group, "sparkbar.scene"};
    buildDescription(described);
    (void)core::reconcileScene(*inner_, described);
}

void SparkBar::buildDescription(SceneNode& root) const {
    const double radius = options_.cornerRadius;
    const double fontSize = options_.tagFontSize > 0.0 ? options_.tagFontSize : 12.0;

    // 1. 每条横向百分比堆叠条 + 上方行标题（如仓库/模块名）
    for (std::size_t i = 0; i < layout_.items.size(); ++i) {
        const core::SparkBarItem& item = layout_.items[i];
        const std::string rowPrefix = "row." + std::to_string(i);

        // 行标题（左对齐在横向条正上方）
        if (options_.showTags && !item.tagRect.empty() && item.tagIndex < data_.tags.size()) {
            const std::string& tag = data_.tags[item.tagIndex];
            if (!tag.empty()) {
                auto tagNode = std::make_unique<SceneNode>(NodeType::Text, rowPrefix + ".tag");
                tagNode->setText(tag);
                tagNode->setFrame(item.tagRect);
                core::SceneStyle style = tagNode->style();
                style.fill = options_.tagColor.visible() ? core::Paint{options_.tagColor}
                                                         : core::Paint{core::ColorToken::Label};
                style.fontSize = fontSize;
                style.textAlign = TextAlign::Left;
                style.textBaseline = TextBaseline::Middle;
                tagNode->setStyle(style);
                root.appendChild(std::move(tagNode));
            }
        }

        // 各语言分段（自左向右按百分比排布）
        for (std::size_t s = 0; s < item.segments.size(); ++s) {
            const core::SparkBarSegment& seg = item.segments[s];
            auto segNode = std::make_unique<SceneNode>(
                NodeType::Rect, rowPrefix + ".seg." + std::to_string(seg.seriesIndex));
            segNode->setFrame(seg.rect);
            core::SceneStyle style = segNode->style();
            style.fill = colorAt(seg.seriesIndex);
            style.cornerRadius = radius;
            segNode->setStyle(style);
            root.appendChild(std::move(segNode));
        }
    }

    // 2. Hover 外描边（压在所有填充段之上，不被相邻段盖住）
    if (options_.hoverBorderWidth > 0.0 && hoverItem_ < layout_.items.size() &&
        hoverSegment_ != core::kInvalidIndex) {
        const core::SparkBarItem& item = layout_.items[hoverItem_];
        if (hoverSegment_ < item.segments.size()) {
            const core::SparkBarSegment& seg = item.segments[hoverSegment_];
            const double effRadius =
                math::clamp(radius, 0.0, std::min(seg.rect.width, seg.rect.height) * 0.5);
            // 内沿向填充体内缩 0.25px 咬合，彻底消除圆角多边形离散化与抗锯齿带来的半像素白缝
            const double half = options_.hoverBorderWidth * 0.5;
            const double expand = std::max(0.0, half - 0.25);
            const RectD outline{seg.rect.x - expand, seg.rect.y - expand,
                                seg.rect.width + expand * 2.0,
                                seg.rect.height + expand * 2.0};
            auto hoverNode = std::make_unique<SceneNode>(NodeType::Rect, "hover.outline");
            hoverNode->setFrame(outline);
            core::SceneStyle style = hoverNode->style();
            style.stroke = Color::mix(colorAt(seg.seriesIndex), Color::Rgb(0.0, 0.0, 0.0), 0.14);
            style.strokeWidth = options_.hoverBorderWidth;
            style.cornerRadius = effRadius + expand;
            hoverNode->setStyle(style);
            root.appendChild(std::move(hoverNode));
        }
    }

    // 3. 底部 GitHub 风格圆点 + 语言占比图例（● C++ 68.4%   ● JavaScript 15.2% ...）
    for (const core::SparkBarLegendItem& leg : layout_.legend) {
        if (leg.seriesIndex >= data_.series.size()) {
            continue;
        }
        const std::string keySuffix = std::to_string(leg.seriesIndex);

        auto dot = std::make_unique<SceneNode>(NodeType::Ellipse, "legend.dot." + keySuffix);
        dot->setFrame(leg.dotRect);
        {
            core::SceneStyle style = dot->style();
            style.fill = colorAt(leg.seriesIndex);
            dot->setStyle(style);
        }
        root.appendChild(std::move(dot));

        char pctBuf[32] = {};
        std::snprintf(pctBuf, sizeof(pctBuf), "  %.1f%%", leg.fraction * 100.0);
        const std::string labelText = data_.series[leg.seriesIndex].name + pctBuf;

        auto text = std::make_unique<SceneNode>(NodeType::Text, "legend.text." + keySuffix);
        text->setText(labelText);
        text->setFrame(leg.textRect);
        {
            core::SceneStyle style = text->style();
            style.fill = options_.tagColor.visible() ? core::Paint{options_.tagColor}
                                                     : core::Paint{core::ColorToken::Label};
            style.fontSize = fontSize;
            style.textAlign = TextAlign::Left;
            style.textBaseline = TextBaseline::Middle;
            text->setStyle(style);
        }
        root.appendChild(std::move(text));
    }
}

}  // namespace kiriyama::scene
