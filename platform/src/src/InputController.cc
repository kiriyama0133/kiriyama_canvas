#include "kiriyama/scene/InputController.h"

#include <algorithm>
#include <utility>

#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

namespace {

[[nodiscard]] bool modsMatch(KeyMod actual, KeyMod expected) noexcept {
    if (expected == KeyMod::Any) {
        return true;
    }
    return actual == expected;
}

[[nodiscard]] Widget* findCapturedWidget(core::SceneNode& node) {
    if (!node.visible()) {
        return nullptr;
    }
    for (std::size_t i = node.childCount(); i > 0; --i) {
        if (core::SceneNode* child = node.childAt(i - 1)) {
            if (Widget* captured = findCapturedWidget(*child)) {
                return captured;
            }
        }
    }
    if (auto* widget = dynamic_cast<Widget*>(&node)) {
        if (widget->hasPointerCapture()) {
            return widget;
        }
    }
    return nullptr;
}

[[nodiscard]] Widget* findDeepestHitWidget(core::SceneNode& node, const core::Vec2d& pointer) {
    if (!node.visible()) {
        return nullptr;
    }
    if (!node.clipRect().empty() && !node.clipRect().contains(node.toLocal(pointer))) {
        return nullptr;
    }
    for (std::size_t i = node.childCount(); i > 0; --i) {
        if (core::SceneNode* child = node.childAt(i - 1)) {
            if (Widget* hit = findDeepestHitWidget(*child, pointer)) {
                return hit;
            }
        }
    }
    if (auto* widget = dynamic_cast<Widget*>(&node)) {
        if (widget->hitTest(pointer)) {
            return widget;
        }
    }
    return nullptr;
}

}  // namespace

InputController::BindingId InputController::bindKeyDown(KeyCode key, ActionFn action, KeyMod mods,
                                                        bool allowRepeat, bool capture) {
    if (!action) {
        return 0;
    }
    const BindingId id = nextId_++;
    keyBindings_.push_back(KeyBinding{
        id,
        key,
        mods,
        true,
        false,
        allowRepeat,
        capture,
        [fn = std::move(action)](const KeyEvent& ev) {
            fn();
            ev.consume();
            return true;
        },
    });
    return id;
}

InputController::BindingId InputController::bindKeyUp(KeyCode key, ActionFn action, KeyMod mods,
                                                      bool capture) {
    if (!action) {
        return 0;
    }
    const BindingId id = nextId_++;
    keyBindings_.push_back(KeyBinding{
        id,
        key,
        mods,
        false,
        true,
        true,
        capture,
        [fn = std::move(action)](const KeyEvent& ev) {
            fn();
            ev.consume();
            return true;
        },
    });
    return id;
}

InputController::BindingId InputController::bindKey(KeyCode key, KeyHandlerFn handler, KeyMod mods,
                                                    bool capture) {
    if (!handler) {
        return 0;
    }
    const BindingId id = nextId_++;
    keyBindings_.push_back(KeyBinding{
        id,
        key,
        mods,
        false,
        false,
        true,
        capture,
        std::move(handler),
    });
    return id;
}

InputController::BindingId InputController::bindWheel(WheelHandlerFn handler, bool capture) {
    if (!handler) {
        return 0;
    }
    const BindingId id = nextId_++;
    wheelBindings_.push_back(WheelBinding{id, capture, std::move(handler)});
    return id;
}

bool InputController::unbind(BindingId id) {
    if (id == 0) {
        return false;
    }
    const auto keyIt = std::remove_if(keyBindings_.begin(), keyBindings_.end(),
                                      [id](const KeyBinding& b) { return b.id == id; });
    if (keyIt != keyBindings_.end()) {
        keyBindings_.erase(keyIt, keyBindings_.end());
        return true;
    }
    const auto wheelIt = std::remove_if(wheelBindings_.begin(), wheelBindings_.end(),
                                         [id](const WheelBinding& b) { return b.id == id; });
    if (wheelIt != wheelBindings_.end()) {
        wheelBindings_.erase(wheelIt, wheelBindings_.end());
        return true;
    }
    return false;
}

void InputController::clear() noexcept {
    keyBindings_.clear();
    wheelBindings_.clear();
}

bool InputController::handleKey(const KeyEvent& event, bool capturePhase) const {
    const auto snapshot = keyBindings_;
    bool consumed = false;
    for (const KeyBinding& binding : snapshot) {
        if (binding.capture != capturePhase) {
            continue;
        }
        if (binding.onlyDown && !event.down) {
            continue;
        }
        if (binding.onlyUp && event.down) {
            continue;
        }
        if (!binding.allowRepeat && event.repeat) {
            continue;
        }
        if (binding.key != KeyCode::Unknown && binding.key != event.key) {
            continue;
        }
        if (!modsMatch(event.mods, binding.mods)) {
            continue;
        }
        if (binding.handler && binding.handler(event)) {
            event.consume();
            consumed = true;
        }
    }
    return consumed || event.handled;
}

bool InputController::handleWheel(const WheelEvent& event, bool capturePhase) const {
    const auto snapshot = wheelBindings_;
    bool consumed = false;
    for (const WheelBinding& binding : snapshot) {
        if (binding.capture != capturePhase) {
            continue;
        }
        if (binding.handler && binding.handler(event)) {
            event.consume();
            consumed = true;
        }
    }
    return consumed || event.handled;
}

Widget* findTopmostWidgetAt(core::SceneNode& root, const core::Vec2d& pointer) {
    if (Widget* captured = findCapturedWidget(root)) {
        return captured;
    }
    return findDeepestHitWidget(root, pointer);
}

std::vector<Widget*> buildWidgetEventPath(Widget* target) {
    std::vector<Widget*> path;
    for (core::SceneNode* curr = target; curr != nullptr; curr = curr->parent()) {
        if (auto* widget = dynamic_cast<Widget*>(curr)) {
            path.push_back(widget);
        }
    }
    std::reverse(path.begin(), path.end());
    return path;
}

bool dispatchKeyToTree(core::SceneNode& root, KeyEvent event, const core::Vec2d& pointer) {
    Widget* target = findTopmostWidgetAt(root, pointer);
    if (target == nullptr) {
        return false;
    }
    const std::vector<Widget*> path = buildWidgetEventPath(target);
    event.target = target;

    // 1. 隧道捕获阶段（Tunnel：从根向叶，不含 target）
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        event.phase = EventPhase::Tunnel;
        event.currentTarget = path[i];
        if (path[i]->dispatchKey(event) || event.handled) {
            return true;
        }
    }

    // 2. 目标阶段（Target：最深层命中节点）
    event.phase = EventPhase::Target;
    event.currentTarget = target;
    if (target->dispatchKey(event) || event.handled) {
        return true;
    }

    // 3. 冒泡阶段（Bubble：从 target 的父节点向上回传至根）
    for (std::size_t i = path.size() - 1; i > 0; --i) {
        Widget* ancestor = path[i - 1];
        event.phase = EventPhase::Bubble;
        event.currentTarget = ancestor;
        if (ancestor->dispatchKey(event) || event.handled) {
            return true;
        }
    }

    return false;
}

bool dispatchWheelToTree(core::SceneNode& root, WheelEvent event) {
    Widget* target = findTopmostWidgetAt(root, event.pointer);
    if (target == nullptr) {
        return false;
    }
    const std::vector<Widget*> path = buildWidgetEventPath(target);
    event.target = target;

    // 1. 隧道捕获阶段（Tunnel）
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        event.phase = EventPhase::Tunnel;
        event.currentTarget = path[i];
        if (path[i]->dispatchWheel(event) || event.handled) {
            return true;
        }
    }

    // 2. 目标阶段（Target）
    event.phase = EventPhase::Target;
    event.currentTarget = target;
    if (target->dispatchWheel(event) || event.handled) {
        return true;
    }

    // 3. 冒泡阶段（Bubble）
    for (std::size_t i = path.size() - 1; i > 0; --i) {
        Widget* ancestor = path[i - 1];
        event.phase = EventPhase::Bubble;
        event.currentTarget = ancestor;
        if (ancestor->dispatchWheel(event) || event.handled) {
            return true;
        }
    }

    return false;
}

}  // namespace kiriyama::scene
