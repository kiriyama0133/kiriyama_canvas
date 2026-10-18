#include "kiriyama/scene/PieSlice.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace kiriyama::scene {

using core::RectD;

namespace {

/// 外扩进/出的时长（秒）。进出共用，进度线性趋近目标。
constexpr double kExplodeDuration = 0.12;

}  // namespace

PieSlice::PieSlice(std::string key) : TooltipTarget(std::move(key)) {}

// ---------------------------------------------------------------- 几何 / 数据

void PieSlice::setArc(const ArcD& arc) noexcept { arc_ = arc; }

void PieSlice::setData(std::string label, double value, double fraction) noexcept {
    label_ = std::move(label);
    value_ = value;
    fraction_ = fraction;
}

// ---------------------------------------------------------------- 外扩

void PieSlice::onUpdate(const FrameContext& ctx) {
    const double target = hovered() ? 1.0 : 0.0;
    if (explodeProgress_ == target) {
        return;
    }
    const double step = ctx.deltaTime / kExplodeDuration;
    if (explodeProgress_ < target) {
        explodeProgress_ = std::min(target, explodeProgress_ + step);
    } else {
        explodeProgress_ = std::max(target, explodeProgress_ - step);
    }
}

// ---------------------------------------------------------------- 命中

bool PieSlice::hitShape(const Vec2d& worldPoint) const noexcept {
    // 命中区跟着外扩走：外半径加上当前半径增量，圆心与角度边界不动。
    const Vec2d local = toLocal(worldPoint);
    if (arc_.contains(local, 0.0)) {
        return true;
    }
    const double radius = explodeRadius();
    if (radius <= 0.0) {
        return false;
    }
    ArcD grown = arc_;
    grown.outerRadius += radius;
    return grown.contains(local, 0.0);
}

// ---------------------------------------------------------------- 气泡

TooltipText PieSlice::tooltipContent() const {
    // 静态内容优先（TooltipTarget 的入口一），没有才用默认的"类 + 数量 · 占比"。
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return TooltipText{tipTitle_, tipBody_};
    }

    char body[96] = {};
    if (std::isfinite(value_)) {
        if (value_ == static_cast<double>(static_cast<long long>(value_))) {
            std::snprintf(body, sizeof(body), "%lld · %.1f%%",
                          static_cast<long long>(value_), fraction_ * 100.0);
        } else {
            std::snprintf(body, sizeof(body), "%.2f · %.1f%%", value_, fraction_ * 100.0);
        }
    }
    return TooltipText{label_, body};
}

bool PieSlice::hasTooltipContent() const {
    if (!tipTitle_.empty() || !tipBody_.empty()) {
        return true;
    }
    // 有数据（哪怕 label 为空）就该能弹：数值本身就是内容。
    return !label_.empty() || value_ != 0.0;
}

RectD PieSlice::tooltipAnchor() const {
    // 气泡锚在**整张饼图的 frame** 上，而不是扇区自己：hover 哪一片气泡都浮在
    // 图表上方，不会压在扇区上、也不会在相邻扇区之间跳来跳去。
    // 父节点就是 PieChart，它的 frame 定义在同一坐标系里。
    if (const SceneNode* owner = parent()) {
        const RectD& box = owner->frame();
        if (!box.empty()) {
            return box;
        }
    }
    return hitBounds();
}

}  // namespace kiriyama::scene
