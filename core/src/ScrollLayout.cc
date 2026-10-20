#include "kiriyama/core/ScrollLayout.h"

#include <algorithm>
#include <cmath>

namespace kiriyama::core {
namespace {

[[nodiscard]] double sanitizeNonNegative(double value) noexcept {
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

[[nodiscard]] double clampFinite(double value, double lo, double hi) noexcept {
    if (std::isnan(value)) {
        return lo;
    }
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

[[nodiscard]] bool shouldShowBar(Overflow mode, double contentLen, double viewLen) noexcept {
    switch (mode) {
        case Overflow::Hidden:
            return false;
        case Overflow::Scroll:
            return viewLen > 0.0;
        case Overflow::Auto:
            return viewLen > 0.0 && contentLen > viewLen + 1e-6;
    }
    return false;
}

/// 递归计算单个子节点在其父坐标系下的包围盒包络（优先使用非空 frame，其次使用 bounds，
/// 对无 frame 的纯 Group 容器递归汇总其子孙节点）。
[[nodiscard]] RectD nodeEnvelopeInParent(const SceneNode& node) noexcept {
    if (!node.visible()) {
        return {};
    }

    RectD localBox{};
    if (!node.frame().empty()) {
        localBox = node.frame();
    } else if (node.type() != NodeType::Group) {
        localBox = node.bounds();
    } else {
        bool hasAny = false;
        double minX = 0.0;
        double minY = 0.0;
        double maxX = 0.0;
        double maxY = 0.0;
        for (std::size_t i = 0; i < node.childCount(); ++i) {
            const SceneNode* child = node.childAt(i);
            if (child == nullptr) {
                continue;
            }
            const RectD childEnv = nodeEnvelopeInParent(*child);
            if (childEnv.empty()) {
                continue;
            }
            if (!hasAny) {
                minX = childEnv.x;
                minY = childEnv.y;
                maxX = childEnv.right();
                maxY = childEnv.bottom();
                hasAny = true;
            } else {
                minX = std::min(minX, childEnv.x);
                minY = std::min(minY, childEnv.y);
                maxX = std::max(maxX, childEnv.right());
                maxY = std::max(maxY, childEnv.bottom());
            }
        }
        if (hasAny) {
            localBox = RectD{minX, minY, maxX - minX, maxY - minY};
        }
    }

    if (localBox.empty()) {
        return {};
    }
    return transformedBounds(node.transform(), localBox);
}

}  // namespace

double ScrollAxisBar::clampScroll(double offset) const noexcept {
    if (!scrollable || !(maxScroll > 0.0)) {
        return 0.0;
    }
    return clampFinite(offset, 0.0, maxScroll);
}

double ScrollAxisBar::scrollForProgress(double normalizedProgress) const noexcept {
    if (!scrollable || !(maxScroll > 0.0)) {
        return 0.0;
    }
    return clampFinite(normalizedProgress, 0.0, 1.0) * maxScroll;
}

double ScrollAxisBar::scrollDeltaFromThumbDelta(double thumbDelta,
                                                bool isVertical) const noexcept {
    if (!scrollable || !(maxScroll > 0.0) || !std::isfinite(thumbDelta)) {
        return 0.0;
    }
    const double trackLen = isVertical ? track.height : track.width;
    const double thumbLen = isVertical ? thumb.height : thumb.width;
    const double travel = trackLen - thumbLen;
    if (!(travel > 1e-6)) {
        return 0.0;
    }
    return thumbDelta * (maxScroll / travel);
}

double ScrollAxisBar::scrollForTrackPosition(double axisCoord,
                                             bool isVertical) const noexcept {
    if (!scrollable || !(maxScroll > 0.0) || !std::isfinite(axisCoord)) {
        return scroll;
    }
    const double trackStart = isVertical ? track.y : track.x;
    const double trackLen = isVertical ? track.height : track.width;
    const double thumbLen = isVertical ? thumb.height : thumb.width;
    const double travel = trackLen - thumbLen;
    if (!(travel > 1e-6)) {
        return 0.0;
    }
    // 让滑块中心对齐到点击坐标 axisCoord
    const double desiredThumbStart = (axisCoord - trackStart) - thumbLen * 0.5;
    const double normalized = clampFinite(desiredThumbStart / travel, 0.0, 1.0);
    return normalized * maxScroll;
}

ScrollLayout computeScrollLayout(const ScrollLayoutInput& input) noexcept {
    ScrollLayout out;

    const double containerW = sanitizeNonNegative(input.containerSize.x);
    const double containerH = sanitizeNonNegative(input.containerSize.y);
    const double contentW = sanitizeNonNegative(input.contentSize.x);
    const double contentH = sanitizeNonNegative(input.contentSize.y);

    out.contentSize = {contentW, contentH};
    if (containerW <= 0.0 || containerH <= 0.0) {
        return out;
    }

    const double thickness = sanitizeNonNegative(input.metrics.thickness);
    const double margin = sanitizeNonNegative(input.metrics.margin);
    const double minThumb = std::max(4.0, sanitizeNonNegative(input.metrics.minThumbLength));
    const double channelSpan = thickness + 2.0 * margin;

    // 1. 判定横纵滚动条可见性与视口尺寸（非悬浮模式下做二阶段收敛判定）
    double viewW = containerW;
    double viewH = containerH;
    bool showH = shouldShowBar(input.overflowX, contentW, viewW);
    bool showV = shouldShowBar(input.overflowY, contentH, viewH);

    if (!input.overlayScrollbars && channelSpan > 0.0) {
        if (showV) {
            viewW = std::max(0.0, containerW - channelSpan);
        }
        if (showH) {
            viewH = std::max(0.0, containerH - channelSpan);
        }
        // 二阶段复核：一轴扣减视口后，可能触发另一轴在 Auto 模式下被动溢出
        const bool nextShowH = shouldShowBar(input.overflowX, contentW, viewW);
        const bool nextShowV = shouldShowBar(input.overflowY, contentH, viewH);
        if (nextShowH != showH || nextShowV != showV) {
            showH = nextShowH;
            showV = nextShowV;
            viewW = showV ? std::max(0.0, containerW - channelSpan) : containerW;
            viewH = showH ? std::max(0.0, containerH - channelSpan) : containerH;
        }
    }

    out.viewport = RectD{0.0, 0.0, viewW, viewH};

    // 2. 计算横向（X 轴）滚动状态
    out.horizontal.visible = showH && thickness > 0.0;
    out.horizontal.viewLength = viewW;
    out.horizontal.contentLength = contentW;
    if (input.overflowX != Overflow::Hidden && contentW > viewW + 1e-6 && viewW > 0.0) {
        out.horizontal.scrollable = true;
        out.horizontal.maxScroll = contentW - viewW;
        out.horizontal.scroll = clampFinite(input.scrollOffset.x, 0.0, out.horizontal.maxScroll);
        out.horizontal.progress = out.horizontal.scroll / out.horizontal.maxScroll;
    }

    // 3. 计算纵向（Y 轴）滚动状态
    out.vertical.visible = showV && thickness > 0.0;
    out.vertical.viewLength = viewH;
    out.vertical.contentLength = contentH;
    if (input.overflowY != Overflow::Hidden && contentH > viewH + 1e-6 && viewH > 0.0) {
        out.vertical.scrollable = true;
        out.vertical.maxScroll = contentH - viewH;
        out.vertical.scroll = clampFinite(input.scrollOffset.y, 0.0, out.vertical.maxScroll);
        out.vertical.progress = out.vertical.scroll / out.vertical.maxScroll;
    }

    out.maxScroll = {out.horizontal.maxScroll, out.vertical.maxScroll};
    out.scroll = {out.horizontal.scroll, out.vertical.scroll};
    out.progress = {out.horizontal.progress, out.vertical.progress};

    // 4. 计算轨道与滑块几何（双轴同时可见时自动避让右下角 corner）
    const double reserveRight = out.vertical.visible ? channelSpan : 0.0;
    const double reserveBottom = out.horizontal.visible ? channelSpan : 0.0;

    if (out.vertical.visible) {
        const double trackX = std::max(0.0, containerW - margin - thickness);
        const double trackY = margin;
        const double trackH = std::max(0.0, containerH - 2.0 * margin - reserveBottom);
        out.vertical.track = RectD{trackX, trackY, thickness, trackH};

        if (trackH > 0.0) {
            double thumbH = trackH;
            double thumbY = trackY;
            if (out.vertical.scrollable && contentH > 0.0) {
                const double ratio = std::clamp(viewH / contentH, 0.0, 1.0);
                thumbH = std::clamp(trackH * ratio, std::min(minThumb, trackH), trackH);
                thumbY = trackY + (trackH - thumbH) * out.vertical.progress;
            }
            out.vertical.thumb = RectD{trackX, thumbY, thickness, thumbH};
        }
    }

    if (out.horizontal.visible) {
        const double trackX = margin;
        const double trackY = std::max(0.0, containerH - margin - thickness);
        const double trackW = std::max(0.0, containerW - 2.0 * margin - reserveRight);
        out.horizontal.track = RectD{trackX, trackY, trackW, thickness};

        if (trackW > 0.0) {
            double thumbW = trackW;
            double thumbX = trackX;
            if (out.horizontal.scrollable && contentW > 0.0) {
                const double ratio = std::clamp(viewW / contentW, 0.0, 1.0);
                thumbW = std::clamp(trackW * ratio, std::min(minThumb, trackW), trackW);
                thumbX = trackX + (trackW - thumbW) * out.horizontal.progress;
            }
            out.horizontal.thumb = RectD{thumbX, trackY, thumbW, thickness};
        }
    }

    if (out.vertical.visible && out.horizontal.visible && channelSpan > 0.0) {
        out.corner = RectD{std::max(0.0, containerW - channelSpan),
                           std::max(0.0, containerH - channelSpan),
                           channelSpan, channelSpan};
    }

    return out;
}

Vec2d measureContentBounds(const SceneNode& contentRoot, Vec2d padding) noexcept {
    double maxRight = 0.0;
    double maxBottom = 0.0;
    for (std::size_t i = 0; i < contentRoot.childCount(); ++i) {
        const SceneNode* child = contentRoot.childAt(i);
        if (child == nullptr || !child->visible()) {
            continue;
        }
        const RectD env = nodeEnvelopeInParent(*child);
        if (env.empty()) {
            continue;
        }
        maxRight = std::max(maxRight, env.right());
        maxBottom = std::max(maxBottom, env.bottom());
    }
    if (maxRight > 0.0) {
        maxRight += sanitizeNonNegative(padding.x);
    }
    if (maxBottom > 0.0) {
        maxBottom += sanitizeNonNegative(padding.y);
    }
    return {maxRight, maxBottom};
}

}  // namespace kiriyama::core
