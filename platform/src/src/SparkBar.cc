#include "kiriyama/scene/SparkBar.h"

#include <cstdio>
#include <utility>

#include "kiriyama/core/SceneBuilder.h"  // defaultSeriesPalette

namespace kiriyama::scene {

namespace {

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
    return colors_[seriesIndex % colors_.size()];
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
        // 段命中：柱体内部再细分。tag 行没有段。
        for (std::size_t s = 0; s < item.segments.size(); ++s) {
            if (item.segments[s].rect.contains(local)) {
                pick.segment = s;
                break;
            }
        }
        return pick;
    }
    return pick;
}

bool SparkBar::hitShape(const Vec2d& worldPoint) const noexcept {
    return pickAtLocal(toLocal(worldPoint)).item != core::kInvalidIndex;
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

    const Pick pick = pickAtLocal(toLocal(pointerPosition()));
    if (pick.item == core::kInvalidIndex) {
        return {};
    }

    const core::SparkBarItem& item = layout_.items[pick.item];
    if (item.tagIndex >= data_.tags.size()) {
        return {};
    }
    const std::string& tag = data_.tags[item.tagIndex];

    // 落在某一段：报这一层的值 + 占整柱比例。
    if (pick.segment != core::kInvalidIndex) {
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
        return TooltipText{tag + " · " + name, body};
    }

    // 落在柱上（段缝、或总量为 0 只有标签）：整柱汇总。
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
    // 锚在整块 SparkBar（图表区域）上：hover 哪根柱，气泡都浮在图的上方，
    // 不会压住柱身。想贴到具体柱子上就覆盖这个函数。
    const RectD& box = frame();
    return box.empty() ? hitBounds() : box;
}

// ---------------------------------------------------------------- 重建

void SparkBar::onUpdate(const FrameContext&) {
    if (dataDirty_ || !(frame() == lastFrame_)) {
        rebuild();
    }

    // 指针账本进 onUpdate 之前已经更新完了：这里把"落在哪根柱 / 哪一段"算一遍存下来。
    // 渲染层（hover 描边）和 tooltip 都读这份缓存，不再各自重算、也就不会走偏。
    if (pointerInside()) {
        const Pick pick = pickAtLocal(toLocal(pointerPosition()));
        hoverItem_ = pick.item;
        hoverSegment_ = pick.segment;
    } else {
        hoverItem_ = core::kInvalidIndex;
        hoverSegment_ = core::kInvalidIndex;
    }
}

void SparkBar::rebuild() {
    dataDirty_ = false;
    lastFrame_ = frame();

    layout_ = core::computeSparkBarLayout(frame().size(), data_, options_);

    // computeSparkBarLayout 给的是"以柱图左上角为原点"的纯几何（方便单测），
    // 这里整体平移到节点本地坐标。
    //
    // 必须平移：渲染层画的是本地坐标（世界 = 本地，组件无祖先变换），
    // hitShape 也拿 toLocal() 的本地坐标比对。以前只有渲染层加了 frame 原点、
    // 命中没加，命中区于是整体上移了 frame.y ——柱子下半截点不到、tooltip 不弹。
    const Vec2d origin = frame().origin();
    if (origin.x != 0.0 || origin.y != 0.0) {
        for (core::SparkBarItem& item : layout_.items) {
            if (!item.barRect.empty()) {
                item.barRect = item.barRect.translated(origin);
            }
            if (!item.tagRect.empty()) {
                item.tagRect = item.tagRect.translated(origin);
            }
            for (core::SparkBarSegment& segment : item.segments) {
                if (!segment.rect.empty()) {
                    segment.rect = segment.rect.translated(origin);
                }
            }
        }
    }
}

}  // namespace kiriyama::scene
