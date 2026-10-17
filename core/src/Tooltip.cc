#include "kiriyama/core/Tooltip.h"

#include <algorithm>
#include <cmath>

namespace kiriyama::core {
namespace {

constexpr double kEps = 1e-9;
constexpr double kHalfPixel = 0.5;

[[nodiscard]] constexpr bool isHorizontal(TooltipSide side) noexcept {
    return side == TooltipSide::Above || side == TooltipSide::Below;
}

[[nodiscard]] constexpr TooltipSide opposite(TooltipSide side) noexcept {
    switch (side) {
        case TooltipSide::Above:
            return TooltipSide::Below;
        case TooltipSide::Below:
            return TooltipSide::Above;
        case TooltipSide::Left:
            return TooltipSide::Right;
        case TooltipSide::Right:
            return TooltipSide::Left;
    }
    return side;
}

/// 卡片沿"边"的方向上应该从哪开始。
[[nodiscard]] double alignAlong(TooltipSide side, TooltipAlign align, const RectD& anchor,
                                double extent, double offset) noexcept {
    const double start = isHorizontal(side) ? anchor.left() : anchor.top();
    const double span = isHorizontal(side) ? anchor.width : anchor.height;
    switch (align) {
        case TooltipAlign::Start:
            return start + offset;
        case TooltipAlign::Center:
            return start + span * 0.5 - extent * 0.5 + offset;
        case TooltipAlign::End:
            return start + span - extent + offset;
    }
    return start + offset;
}

[[nodiscard]] RectD placeRect(const RectD& anchor, const Vec2d& size, TooltipSide side,
                              const TooltipOptions& options) noexcept {
    const double along = alignAlong(side, options.align, anchor,
                                    isHorizontal(side) ? size.x : size.y, options.offset);
    switch (side) {
        case TooltipSide::Above:
            return RectD{along, anchor.top() - options.gap - size.y, size.x, size.y};
        case TooltipSide::Below:
            return RectD{along, anchor.bottom() + options.gap, size.x, size.y};
        case TooltipSide::Left:
            return RectD{anchor.left() - options.gap - size.x, along, size.x, size.y};
        case TooltipSide::Right:
            return RectD{anchor.right() + options.gap, along, size.x, size.y};
    }
    return RectD{along, anchor.top(), size.x, size.y};
}

[[nodiscard]] bool fitsViewport(const RectD& rect, const RectD& viewport, double margin) noexcept {
    return rect.left() >= viewport.left() + margin - kEps &&
           rect.right() <= viewport.right() - margin + kEps &&
           rect.top() >= viewport.top() + margin - kEps &&
           rect.bottom() <= viewport.bottom() - margin + kEps;
}

/// 溢出的总像素数。两侧都放不下时用它挑"更不难受"的那侧。
[[nodiscard]] double overflowOf(const RectD& rect, const RectD& viewport, double margin) noexcept {
    double total = 0.0;
    total += std::max(0.0, (viewport.left() + margin) - rect.left());
    total += std::max(0.0, rect.right() - (viewport.right() - margin));
    total += std::max(0.0, (viewport.top() + margin) - rect.top());
    total += std::max(0.0, rect.bottom() - (viewport.bottom() - margin));
    return total;
}

[[nodiscard]] double clampAxis(double value, double low, double high) noexcept {
    // 卡片本身比可用空间还大时（low > high），贴住起始边，让它溢出到另一侧。
    return high >= low ? std::clamp(value, low, high) : low;
}

}  // namespace

TooltipLayout computeTooltipLayout(const RectD& anchor, const Vec2d& contentSize,
                                   const RectD& viewport,
                                   const TooltipOptions& options) noexcept {
    TooltipLayout out;

    const Vec2d size{std::max(0.0, contentSize.x), std::max(0.0, contentSize.y)};
    if (size.x <= 0.0 || size.y <= 0.0) {
        return out;  // 没内容就没气泡
    }

    const double margin = std::max(0.0, options.margin);
    out.visible = true;
    out.side = options.side;

    RectD rect = placeRect(anchor, size, options.side, options);
    if (options.flip && !fitsViewport(rect, viewport, margin)) {
        const TooltipSide altSide = opposite(options.side);
        const RectD alt = placeRect(anchor, size, altSide, options);
        const bool altFits = fitsViewport(alt, viewport, margin);
        if (altFits || overflowOf(alt, viewport, margin) < overflowOf(rect, viewport, margin)) {
            rect = alt;
            out.side = altSide;
            out.flipped = true;
        }
    }

    if (options.clampToViewport) {
        const double nx = clampAxis(rect.x, viewport.left() + margin,
                                    viewport.right() - margin - rect.width);
        const double ny = clampAxis(rect.y, viewport.top() + margin,
                                    viewport.bottom() - margin - rect.height);
        if (std::abs(nx - rect.x) > kEps || std::abs(ny - rect.y) > kEps) {
            rect.x = nx;
            rect.y = ny;
            out.clamped = true;
        }
    }

    out.rect = rect;

    // ---------------------------------------------------------- 箭头
    if (options.arrowWidth > 0.0) {
        const double radius = std::max(0.0, options.cornerRadius);
        const bool horizontal = isHorizontal(out.side);

        // 锚点中心沿边方向的位置，夹到卡片两个圆角之间，免得箭头画进圆角里。
        // 这里**不加** offset：offset 是让整块气泡躲开别的东西，箭头该指的还是锚点中心。
        const double want = horizontal ? anchor.center().x : anchor.center().y;
        const double low = (horizontal ? rect.left() : rect.top()) + radius;
        const double high = (horizontal ? rect.right() : rect.bottom()) - radius;
        const double at = clampAxis(want, low, high);
        out.arrowClamped = std::abs(at - want) > kHalfPixel;

        switch (out.side) {
            case TooltipSide::Above:
                out.arrowBase = Vec2d{at, rect.bottom()};
                out.arrowTip = Vec2d{at, anchor.top()};
                break;
            case TooltipSide::Below:
                out.arrowBase = Vec2d{at, rect.top()};
                out.arrowTip = Vec2d{at, anchor.bottom()};
                break;
            case TooltipSide::Left:
                out.arrowBase = Vec2d{rect.right(), at};
                out.arrowTip = Vec2d{anchor.left(), at};
                break;
            case TooltipSide::Right:
                out.arrowBase = Vec2d{rect.left(), at};
                out.arrowTip = Vec2d{anchor.right(), at};
                break;
        }

        // 卡片被夹回视口后可能压到锚点上，这时箭头会"往回指"，不如不画。
        bool pointsOutward = false;
        switch (out.side) {
            case TooltipSide::Above:
                pointsOutward = out.arrowBase.y <= out.arrowTip.y + kHalfPixel;
                break;
            case TooltipSide::Below:
                pointsOutward = out.arrowBase.y >= out.arrowTip.y - kHalfPixel;
                break;
            case TooltipSide::Left:
                pointsOutward = out.arrowBase.x <= out.arrowTip.x + kHalfPixel;
                break;
            case TooltipSide::Right:
                pointsOutward = out.arrowBase.x >= out.arrowTip.x - kHalfPixel;
                break;
        }

        const Vec2d delta{out.arrowTip.x - out.arrowBase.x, out.arrowTip.y - out.arrowBase.y};
        const double length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        out.hasArrow = pointsOutward && length >= 1.0;
    }

    return out;
}

bool tooltipArrowPolygon(const TooltipLayout& layout, double arrowWidth, Vec2d outPoints[3]) noexcept {
    if (!layout.hasArrow || arrowWidth <= 0.0) {
        return false;
    }

    const Vec2d delta{layout.arrowTip.x - layout.arrowBase.x, layout.arrowTip.y - layout.arrowBase.y};
    const double length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length < 1e-6) {
        return false;
    }

    // 法线方向就是箭头底边方向。
    const double nx = -delta.y / length;
    const double ny = delta.x / length;
    const double half = arrowWidth * 0.5;

    outPoints[0] = layout.arrowTip;
    outPoints[1] = Vec2d{layout.arrowBase.x + nx * half, layout.arrowBase.y + ny * half};
    outPoints[2] = Vec2d{layout.arrowBase.x - nx * half, layout.arrowBase.y - ny * half};
    return true;
}

// ---------------------------------------------------------------- 状态机

void TooltipState::hover(std::uint64_t id, const RectD& anchor, double nowMs) noexcept {
    if (id == 0) {
        if (hoverId_ == 0) {
            return;  // 本来就没悬停，别每帧重置倒计时
        }
        hoverId_ = 0;
        hoverAnchor_ = RectD{};
        // 收起延迟从"离开的那一刻"算起，而不是从下一次 update 才开始计时，
        // 否则调用方隔一会儿才 update 一次会把气泡多挂一段。
        if (visibleId_ != 0 && hideAt_ == kNoDeadline) {
            hideAt_ = nowMs + std::max(0.0, timing_.hideDelay);
        }
        return;
    }
    if (id != hoverId_) {
        hoverId_ = id;
        hoverSince_ = nowMs;  // 只有换目标才重置计时
    }
    hoverAnchor_ = anchor;  // 位置每帧都跟新，指针在目标上移动不会打断倒计时
}

void TooltipState::focus(std::uint64_t id, const RectD& anchor, double nowMs) noexcept {
    if (id == 0) {
        blur(nowMs);
        return;
    }
    if (id != focusId_) {
        focusId_ = id;
        focusSince_ = nowMs;
    }
    focusAnchor_ = anchor;
}

void TooltipState::blur(double nowMs) noexcept {
    if (focusId_ == 0) {
        return;
    }
    focusId_ = 0;
    focusAnchor_ = RectD{};
    // 焦点走了又没有 hover 接着，收起倒计时从这一刻算。
    if (hoverId_ == 0 && visibleId_ != 0 && hideAt_ == kNoDeadline) {
        hideAt_ = nowMs + std::max(0.0, timing_.hideDelay);
    }
}

TooltipChange TooltipState::press(double nowMs) noexcept {
    (void)nowMs;
    if (!timing_.hideOnPress || visibleId_ == 0) {
        return {};
    }
    hoverId_ = 0;  // 按下之后不再算悬停，松手后要重新进入才会再弹
    return hideNow();
}

TooltipChange TooltipState::hide() noexcept {
    hoverId_ = 0;
    return hideNow();
}

void TooltipState::reset() noexcept {
    hoverId_ = 0;
    hoverAnchor_ = RectD{};
    focusId_ = 0;
    focusAnchor_ = RectD{};
    visibleId_ = 0;
    anchor_ = RectD{};
    shownAt_ = 0.0;
    hideAt_ = kNoDeadline;
}

TooltipChange TooltipState::update(double nowMs) noexcept {
    // focus 压过 hover：键盘操作不该被路过的指针顶掉。
    const std::uint64_t desired = focusId_ != 0 ? focusId_ : hoverId_;

    if (desired == visibleId_) {
        hideAt_ = kNoDeadline;
        if (desired != 0) {
            anchor_ = (desired == focusId_) ? focusAnchor_ : hoverAnchor_;
        }
        return {};
    }

    if (desired == 0) {
        if (visibleId_ == 0) {
            hideAt_ = kNoDeadline;
            return {};
        }
        if (hideAt_ == kNoDeadline) {
            hideAt_ = nowMs + std::max(0.0, timing_.hideDelay);
        }
        return nowMs >= hideAt_ ? hideNow() : TooltipChange{};
    }

    const bool fromFocus = desired == focusId_;
    const double since = fromFocus ? focusSince_ : hoverSince_;
    const double delay = std::max(0.0, fromFocus ? timing_.focusDelay : timing_.showDelay);
    if (nowMs - since < delay) {
        return {};
    }
    return show(desired, nowMs);
}

double TooltipState::opacity(double nowMs) const noexcept {
    if (visibleId_ == 0) {
        return 0.0;
    }
    const double fade = std::max(0.0, timing_.fadeIn);
    if (fade <= 0.0) {
        return 1.0;
    }
    const double progress = (nowMs - shownAt_) / fade;
    return std::clamp(progress, 0.0, 1.0);
}

TooltipChange TooltipState::show(std::uint64_t id, double nowMs) noexcept {
    visibleId_ = id;
    shownAt_ = nowMs;
    hideAt_ = kNoDeadline;
    // 生效的锚点在 update() 里已经写过 hoverAnchor_ / focusAnchor_，这里再取一次最新值。
    anchor_ = (id == focusId_) ? focusAnchor_ : hoverAnchor_;
    return TooltipChange{true, true, id};
}

TooltipChange TooltipState::hideNow() noexcept {
    if (visibleId_ == 0) {
        hideAt_ = kNoDeadline;
        return {};
    }
    visibleId_ = 0;
    hideAt_ = kNoDeadline;
    return TooltipChange{true, false, 0};
}

}  // namespace kiriyama::core
