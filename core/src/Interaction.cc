#include "kiriyama/core/Interaction.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kiriyama::core {
namespace {

/// 记录一个需要重绘的节点：标脏 + 去重收集 id。
void addDirty(InteractionChange& change, SceneNode* node) {
    if (node == nullptr) {
        return;
    }
    node->markDirty();
    const std::uint64_t id = node->id();
    if (std::find(change.dirtyIds.begin(), change.dirtyIds.end(), id) == change.dirtyIds.end()) {
        change.dirtyIds.push_back(id);
    }
}

/// top 按自身 alpha 叠加到 base 之上（source-over）。
Color blendOver(const Color& base, const Color& top) noexcept {
    const float alpha = top.a + base.a * (1.0f - top.a);
    if (!(alpha > 0.0f)) {
        return Color{};
    }
    const float weight = base.a * (1.0f - top.a);
    return {(top.r * top.a + base.r * weight) / alpha,
            (top.g * top.a + base.g * weight) / alpha,
            (top.b * top.a + base.b * weight) / alpha, alpha};
}

}  // namespace

// ---------------------------------------------------------------- 构造 / 配置

InteractionState::InteractionState(InteractionOptions options) : options_(std::move(options)) {}

void InteractionState::setOptions(InteractionOptions options) {
    options_ = std::move(options);
    // anchorLevel 变了，旧链可能对不上，直接作废让下一次 apply 重建。
    hoverChain_.clear();
}

void InteractionState::setViewTransform(const math::Affine2D& worldToCanvas) {
    viewTransform_ = worldToCanvas;
}

void InteractionState::clearViewTransform() noexcept { viewTransform_.reset(); }

void InteractionState::reset() {
    pointer_ = {};
    pointerInside_ = false;
    hoveredId_ = 0;
    hoverChain_.clear();
    activeId_ = 0;
    downId_ = 0;
    pressedButton_ = 0;
}

// ---------------------------------------------------------------- 坐标

Vec2d InteractionState::toWorld(const Vec2d& canvasPoint) const noexcept {
    if (!viewTransform_.has_value()) {
        return canvasPoint;
    }
    return viewTransform_->inverse().apply(canvasPoint);
}

// ---------------------------------------------------------------- 命中

SceneNode* InteractionState::pickAnchor(SceneNode* node) const noexcept {
    if (node == nullptr) {
        return nullptr;
    }
    int level = std::max(0, options_.anchorLevel);
    while (level > 0 && node->parent() != nullptr) {
        node = node->parent();
        --level;
    }
    return node;
}

SceneNode* InteractionState::hitAnchor(SceneNode& root, const Vec2d& worldPoint) {
    SceneHit hit = hitTestScene(root, worldPoint, options_.hitTolerance);
    if (!hit) {
        return nullptr;
    }
    return pickAnchor(hit.node);
}

// ---------------------------------------------------------------- 状态设置

InteractionChange InteractionState::setHover(SceneNode& root, SceneNode* anchor,
                                             InteractionChange& change) {
    const std::uint64_t next = anchor != nullptr ? anchor->id() : 0;
    if (next == hoveredId_) {
        change.hoveredId = hoveredId_;
        return change;
    }

    SceneNode* previous = hoveredId_ != 0 ? findById(root, hoveredId_) : nullptr;
    hoveredId_ = next;
    change.hoveredId = next;
    change.hoverChanged = true;
    addDirty(change, previous);
    addDirty(change, anchor);

    hoverChain_.clear();
    if (next != 0 && options_.trackChain) {
        for (const SceneNode* node = anchor; node != nullptr; node = node->parent()) {
            hoverChain_.push_back(node->id());
        }
        std::reverse(hoverChain_.begin(), hoverChain_.end());
    }
    return change;
}

InteractionChange InteractionState::setActive(SceneNode& root, SceneNode* anchor,
                                              InteractionChange& change) {
    const std::uint64_t next = anchor != nullptr ? anchor->id() : 0;
    if (next == activeId_) {
        change.activeId = activeId_;
        return change;
    }

    SceneNode* previous = activeId_ != 0 ? findById(root, activeId_) : nullptr;
    activeId_ = next;
    change.activeId = next;
    change.activeChanged = true;
    addDirty(change, previous);
    addDirty(change, anchor);
    return change;
}

void InteractionState::pruneMissing(SceneNode& root, InteractionChange& change) {
    if (hoveredId_ != 0 && findById(root, hoveredId_) == nullptr) {
        hoveredId_ = 0;
        hoverChain_.clear();
        change.hoverChanged = true;
        change.hoveredId = 0;
    }
    if (activeId_ != 0 && findById(root, activeId_) == nullptr) {
        change.releasedId = activeId_;
        activeId_ = 0;
        change.activeChanged = true;
        change.activeId = 0;
    }
    if (downId_ != 0 && findById(root, downId_) == nullptr) {
        downId_ = 0;
    }
}

// ---------------------------------------------------------------- 事件入口

InteractionChange InteractionState::apply(SceneNode& root, const PointerEvent& event) {
    InteractionChange change;

    pruneMissing(root, change);

    const Vec2d world = toWorld(event.position);
    change.pointerMoved = !(world.x == pointer_.x && world.y == pointer_.y);
    pointer_ = world;
    if (event.type != PointerEventType::Leave) {
        pointerInside_ = true;
    }

    switch (event.type) {
        case PointerEventType::Enter:
        case PointerEventType::Move:
            // 按下期间不做重新命中：等价于指针捕获，拖出节点也不会丢 active。
            if (activeId_ == 0) {
                setHover(root, hitAnchor(root, world), change);
            }
            break;

        case PointerEventType::Leave:
            pointerInside_ = false;
            if (activeId_ == 0) {
                setHover(root, nullptr, change);
            }
            break;

        case PointerEventType::Down: {
            SceneNode* anchor = hitAnchor(root, world);
            setActive(root, anchor, change);
            downId_ = activeId_;
            pressedButton_ = event.button;
            setHover(root, anchor, change);
            break;
        }

        case PointerEventType::Up: {
            SceneNode* anchor = hitAnchor(root, world);
            const std::uint64_t released = activeId_;
            if (released != 0) {
                change.releasedId = released;
                if (downId_ != 0 && anchor != nullptr && anchor->id() == downId_) {
                    change.clickedId = downId_;
                }
            }
            downId_ = 0;
            pressedButton_ = 0;
            setActive(root, nullptr, change);
            setHover(root, anchor, change);
            break;
        }

        case PointerEventType::Cancel:
            if (activeId_ != 0) {
                change.releasedId = activeId_;
            }
            downId_ = 0;
            pressedButton_ = 0;
            setActive(root, nullptr, change);
            if (pointerInside_) {
                setHover(root, hitAnchor(root, world), change);
            }
            break;
    }

    change.pointerInside = pointerInside_;
    return change;
}

// ---------------------------------------------------------------- 查询

bool InteractionState::isHovered(const SceneNode& node) const noexcept {
    if (hoveredId_ == 0) {
        return false;
    }
    for (const SceneNode* current = &node; current != nullptr; current = current->parent()) {
        if (current->id() == hoveredId_) {
            return true;
        }
    }
    return false;
}

bool InteractionState::isActive(const SceneNode& node) const noexcept {
    if (activeId_ == 0) {
        return false;
    }
    for (const SceneNode* current = &node; current != nullptr; current = current->parent()) {
        if (current->id() == activeId_) {
            return true;
        }
    }
    return false;
}

InteractionStyle InteractionState::styleFor(const SceneNode& node) const noexcept {
    if (isActive(node)) {
        return options_.active;
    }
    if (isHovered(node)) {
        return options_.hover;
    }
    return {};
}

bool InteractionState::wantsPointerCursor() const noexcept {
    if (hoveredId_ == 0) {
        return false;
    }
    return (activeId_ != 0 ? options_.active : options_.hover).pointerCursor;
}

// ---------------------------------------------------------------- 样式合成

SceneStyle applyInteractionStyle(const SceneStyle& base,
                                 const InteractionStyle& overlay) noexcept {
    SceneStyle result = base;

    if (overlay.overlayFill.a > 0.0f) {
        // 直接色：正常混色。token 色：core 不知道当前主题，混色无意义——
        // overlay 是显式意图，直接覆盖成 overlay 的颜色。
        if (base.fill.token == ColorToken::None) {
            result.fill = blendOver(base.fill.color, overlay.overlayFill);
        } else {
            result.fill = overlay.overlayFill;
        }
    }

    if (overlay.overlayStroke.a > 0.0f) {
        result.stroke = overlay.overlayStroke;
        // 原样式没有描边时，delta 才是有效宽度。
        const double width = base.strokeWidth > 0.0 ? base.strokeWidth : 1.0;
        result.strokeWidth = std::max(0.0, width + overlay.strokeWidthDelta);
    }

    const double scale = overlay.opacityScale >= 0.0 ? overlay.opacityScale : 1.0;
    result.opacity = static_cast<double>(math::saturate(base.opacity * scale));
    return result;
}

math::Affine2D interactionTransform(const SceneNode& node,
                                    const InteractionStyle& overlay) noexcept {
    if (!(overlay.scale > 0.0) || std::abs(overlay.scale - 1.0) <= 1e-9) {
        return math::Affine2D::Identity();
    }
    RectD box = node.localBounds();
    if (box.empty()) {
        box = node.bounds();
    }
    if (box.empty()) {
        return math::Affine2D::Identity();
    }
    const Vec2d center = box.center();
    return math::Affine2D::Translation(center) *
           math::Affine2D::Scaling({overlay.scale, overlay.scale}) *
           math::Affine2D::Translation(-center);
}

}  // namespace kiriyama::core
