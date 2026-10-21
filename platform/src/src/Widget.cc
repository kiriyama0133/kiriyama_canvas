#include "kiriyama/scene/Widget.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace kiriyama::scene {

namespace {

using core::Affine2D;

/// 把本地 AABB 的四个角变到世界坐标，再求轴对齐外接框。
[[nodiscard]] RectD toWorldBounds(const RectD& local, const Affine2D& transform) noexcept {
    if (local.empty()) {
        return {};
    }
    const Vec2d c0 = transform.apply({local.left(), local.top()});
    const Vec2d c1 = transform.apply({local.right(), local.top()});
    const Vec2d c2 = transform.apply({local.right(), local.bottom()});
    const Vec2d c3 = transform.apply({local.left(), local.bottom()});

    const double minX = std::min({c0.x, c1.x, c2.x, c3.x});
    const double maxX = std::max({c0.x, c1.x, c2.x, c3.x});
    const double minY = std::min({c0.y, c1.y, c2.y, c3.y});
    const double maxY = std::max({c0.y, c1.y, c2.y, c3.y});
    return RectD{minX, minY, maxX - minX, maxY - minY};
}

}  // namespace

Widget::Widget(std::string key) : SceneNode(core::NodeType::Group, std::move(key)) {}

// ---------------------------------------------------------------- 边框与阴影

Widget& Widget::setCornerRadius(double radius) noexcept {
    core::SceneStyle next = style();
    next.cornerRadius = std::max(0.0, radius);
    setStyle(next);
    return *this;
}

Widget& Widget::setBorder(const core::Paint& color, double width, double cornerRadius) noexcept {
    core::SceneStyle next = style();
    next.stroke = color;
    next.strokeWidth = std::max(0.0, width);
    if (cornerRadius >= 0.0) {
        next.cornerRadius = cornerRadius;
    }
    setStyle(next);
    return *this;
}

Widget& Widget::clearBorder() noexcept {
    core::SceneStyle next = style();
    next.stroke = core::Paint{};
    next.strokeWidth = 0.0;
    setStyle(next);
    return *this;
}

Widget& Widget::setShadow(const core::BoxShadow& shadow) noexcept {
    core::SceneStyle next = style();
    next.shadow = shadow;
    next.shadow.blur = std::max(0.0, next.shadow.blur);
    setStyle(next);
    return *this;
}

Widget& Widget::setShadow(const core::Paint& color, const Vec2d& offset, double blur,
                          double spread) noexcept {
    return setShadow(core::BoxShadow{color, offset, std::max(0.0, blur), spread});
}

Widget& Widget::clearShadow() noexcept {
    core::SceneStyle next = style();
    next.shadow = core::BoxShadow{};
    setStyle(next);
    return *this;
}

// ---------------------------------------------------------------- 输入控制器（Tunnel / Bubble）

Widget& Widget::bindKeyDown(KeyCode key, InputController::ActionFn action, KeyMod mods,
                            bool allowRepeat) {
    (void)input_.bindKeyDown(key, std::move(action), mods, allowRepeat, false);
    return *this;
}

Widget& Widget::bindKeyUp(KeyCode key, InputController::ActionFn action, KeyMod mods) {
    (void)input_.bindKeyUp(key, std::move(action), mods, false);
    return *this;
}

Widget& Widget::bindKey(KeyCode key, InputController::KeyHandlerFn handler, KeyMod mods) {
    (void)input_.bindKey(key, std::move(handler), mods, false);
    return *this;
}

Widget& Widget::bindWheel(InputController::WheelHandlerFn handler) {
    (void)input_.bindWheel(std::move(handler), false);
    return *this;
}

Widget& Widget::onClick(InputController::ActionFn action) {
    if (action) {
        clickHandlers_.push_back(std::move(action));
    }
    return *this;
}

Widget& Widget::captureKeyDown(KeyCode key, InputController::ActionFn action, KeyMod mods,
                               bool allowRepeat) {
    (void)input_.bindKeyDown(key, std::move(action), mods, allowRepeat, true);
    return *this;
}

Widget& Widget::captureKeyUp(KeyCode key, InputController::ActionFn action, KeyMod mods) {
    (void)input_.bindKeyUp(key, std::move(action), mods, true);
    return *this;
}

Widget& Widget::captureKey(KeyCode key, InputController::KeyHandlerFn handler, KeyMod mods) {
    (void)input_.bindKey(key, std::move(handler), mods, true);
    return *this;
}

Widget& Widget::captureWheel(InputController::WheelHandlerFn handler) {
    (void)input_.bindWheel(std::move(handler), true);
    return *this;
}

bool Widget::dispatchKey(const KeyEvent& event) {
    if (event.phase == EventPhase::Tunnel) {
        const bool handledByController = input_.handleKey(event, true);
        const bool handledByHook = onPreviewKey(event);
        if (handledByController || handledByHook) {
            event.consume();
            return true;
        }
        return event.handled;
    }
    const bool handledByController = input_.handleKey(event, false);
    const bool handledByHook = onKey(event);
    if (handledByController || handledByHook) {
        event.consume();
        return true;
    }
    return event.handled;
}

bool Widget::dispatchWheel(const WheelEvent& event) {
    if (event.phase == EventPhase::Tunnel) {
        const bool handledByController = input_.handleWheel(event, true);
        const bool handledByHook = onPreviewWheel(event);
        if (handledByController || handledByHook) {
            event.consume();
            return true;
        }
        return event.handled;
    }
    const bool handledByController = input_.handleWheel(event, false);
    const bool handledByHook = onWheel(event);
    if (handledByController || handledByHook) {
        event.consume();
        return true;
    }
    return event.handled;
}

bool Widget::dispatchPointer(const PointerEvent& event) {
    if (event.phase == EventPhase::Tunnel) {
        if (onPreviewPointer(event)) {
            event.consume();
            return true;
        }
        return event.handled;
    }
    if (onPointer(event) || stopPointerPropagation_) {
        event.consume();
        return true;
    }
    return event.handled;
}

// ---------------------------------------------------------------- 命中区与光标查询

RectD Widget::localHitArea() const noexcept {
    const RectD& box = frame();
    if (!box.empty()) {
        return box;
    }
    return bounds();
}

RectD Widget::hitBounds() const noexcept {
    const RectD area = localHitArea();
    if (area.empty()) {
        return {};
    }
    return toWorldBounds(area, worldTransform());
}

bool Widget::hitShape(const Vec2d& worldPoint) const noexcept {
    return hitBounds().contains(worldPoint);
}

bool Widget::hitTest(const Vec2d& worldPoint) const noexcept {
    for (const SceneNode* curr = this; curr != nullptr; curr = curr->parent()) {
        if (!curr->clipRect().empty() && !curr->clipRect().contains(curr->toLocal(worldPoint))) {
            return false;
        }
    }
    return hitShape(worldPoint);
}

namespace {

[[nodiscard]] const Widget* findCapturedInSubtree(const SceneNode& node) noexcept {
    if (!node.visible()) {
        return nullptr;
    }
    for (std::size_t i = node.childCount(); i > 0; --i) {
        if (const SceneNode* child = node.childAt(i - 1)) {
            if (const Widget* captured = findCapturedInSubtree(*child)) {
                return captured;
            }
        }
    }
    if (const auto* widget = dynamic_cast<const Widget*>(&node)) {
        if (widget->hasPointerCapture()) {
            return widget;
        }
    }
    return nullptr;
}

[[nodiscard]] core::CursorShape resolveSubtreeCursor(const SceneNode& node, const Vec2d& worldPoint,
                                                     bool& consumed) noexcept {
    if (!node.visible()) {
        return core::CursorShape::Default;
    }
    if (!node.clipRect().empty() && !node.clipRect().contains(node.toLocal(worldPoint))) {
        return core::CursorShape::Default;
    }
    for (std::size_t i = node.childCount(); i > 0; --i) {
        if (const SceneNode* child = node.childAt(i - 1)) {
            const core::CursorShape c = resolveSubtreeCursor(*child, worldPoint, consumed);
            if (consumed || c != core::CursorShape::Default) {
                return c;
            }
        }
    }
    if (const auto* widget = dynamic_cast<const Widget*>(&node)) {
        if (widget->hitTest(worldPoint)) {
            if (widget->stopPointerPropagation()) {
                consumed = true;
            }
            const core::CursorShape wc = widget->cursorAt(worldPoint);
            if (wc != core::CursorShape::Default) {
                return wc;
            }
        }
    }
    return core::CursorShape::Default;
}

}  // namespace

core::CursorShape Widget::childCursorAt(const Vec2d& worldPoint,
                                        bool* consumedByChild) const noexcept {
    if (consumedByChild != nullptr) {
        *consumedByChild = false;
    }
    // 1. 若有子 Widget 正在捕获指针（如正在拖拽滑块），由该子 Widget 独占光标
    for (std::size_t i = childCount(); i > 0; --i) {
        if (const SceneNode* child = childAt(i - 1)) {
            if (const Widget* captured = findCapturedInSubtree(*child)) {
                if (consumedByChild != nullptr) {
                    *consumedByChild = true;
                }
                return captured->style().cursor;
            }
        }
    }
    // 2. 逆序查找命中的子 Widget
    bool consumed = false;
    for (std::size_t i = childCount(); i > 0; --i) {
        if (const SceneNode* child = childAt(i - 1)) {
            const core::CursorShape c = resolveSubtreeCursor(*child, worldPoint, consumed);
            if (consumed || c != core::CursorShape::Default) {
                if (consumedByChild != nullptr) {
                    *consumedByChild = consumed;
                }
                return c;
            }
        }
    }
    return core::CursorShape::Default;
}

core::CursorShape Widget::cursorAt(const Vec2d& worldPoint) const noexcept {
    if (!hitTest(worldPoint)) {
        return core::CursorShape::Default;
    }
    bool consumedByChild = false;
    const core::CursorShape childCursor = childCursorAt(worldPoint, &consumedByChild);
    if (consumedByChild || childCursor != core::CursorShape::Default) {
        return childCursor;
    }
    return style().cursor;
}

// ---------------------------------------------------------------- 驱动与三阶段指针路由

void Widget::updatePresence(bool occludedByChild) {
    inside_ = !occludedByChild && hitTest(pointer_);
    const bool active = inside_ || captured_;
    if (active == wasVisible_) {
        return;
    }
    wasVisible_ = active;
    if (active) {
        onPointerEnter();
    } else {
        onPointerLeave();
    }
}

void Widget::tick(const FrameContext& ctx, bool occludedByChild) {
    const Vec2d position = ctx.pointer.position;
    pointerDelta_ = hasPointer_ ? Vec2d{position.x - pointer_.x, position.y - pointer_.y} : Vec2d{};
    pointer_ = position;
    hasPointer_ = true;

    updatePresence(occludedByChild);

    clicked_ = false;
    const bool down = ctx.pointer.down;
    if (down && !down_) {
        pressed_ = inside_;
        if (pressed_) {
            onPress();
        }
    } else if (!down && down_) {
        const bool wasPressed = pressed_;
        pressed_ = false;
        clicked_ = wasPressed && inside_;
        captured_ = false;
        if (wasPressed) {
            onRelease(inside_);
        }
        if (clicked_ && !clickHandlers_.empty()) {
            const auto snapshot = clickHandlers_;
            for (const auto& handler : snapshot) {
                if (handler) {
                    handler();
                }
            }
        }
    }
    down_ = down;

    // 松手会顺带解除捕获，此时指针可能已经不在组件上了——再算一次才能补上 leave。
    updatePresence(occludedByChild);

    onUpdate(ctx);
}

namespace {

/// 对子树执行指针事件的三阶段路由（Tunnel -> Target -> Bubble），
/// 返回所有被更深层消费者（或隧道拦截者）屏蔽的 Widget 集合。
[[nodiscard]] std::unordered_set<const Widget*> routePointerEventInTree(
    Widget& root, const FrameContext& ctx) {
    std::unordered_set<const Widget*> occluded;

    Widget* target = findTopmostWidgetAt(root, ctx.pointer.position);
    if (target == nullptr) {
        return occluded;
    }

    const std::vector<Widget*> path = buildWidgetEventPath(target);
    if (path.empty()) {
        return occluded;
    }

    PointerEvent event;
    event.target = target;
    event.position = ctx.pointer.position;
    event.down = ctx.pointer.down;
    event.type = PointerEventType::Move;

    // 1. 隧道捕获阶段（Tunnel：从根向叶）
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        event.phase = EventPhase::Tunnel;
        event.currentTarget = path[i];
        if (path[i]->dispatchPointer(event) || event.handled) {
            // 若父节点在隧道阶段拦截了指针，则其下方的所有后代节点被屏蔽
            for (std::size_t j = i + 1; j < path.size(); ++j) {
                occluded.insert(path[j]);
            }
            return occluded;
        }
    }

    // 2. 目标阶段（Target：最深层命中节点）
    event.phase = EventPhase::Target;
    event.currentTarget = target;
    if (target->dispatchPointer(event) || event.handled) {
        // 最深层目标消费了指针（如滑块 Drag 或按钮），屏蔽路径上它的所有祖先节点
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            occluded.insert(path[i]);
        }
        return occluded;
    }

    // 3. 冒泡阶段（Bubble：从叶向根）
    for (std::size_t i = path.size() - 1; i > 0; --i) {
        Widget* ancestor = path[i - 1];
        event.phase = EventPhase::Bubble;
        event.currentTarget = ancestor;
        if (ancestor->dispatchPointer(event) || event.handled) {
            // 某个中间祖先消费了指针，屏蔽比它更外层的祖先节点
            for (std::size_t j = 0; j < i - 1; ++j) {
                occluded.insert(path[j]);
            }
            return occluded;
        }
    }

    return occluded;
}

void tickSubtree(SceneNode& node, const FrameContext& ctx,
                 const std::unordered_set<const Widget*>& occluded) {
    if (auto* widget = dynamic_cast<Widget*>(&node)) {
        const bool isOccluded = occluded.find(widget) != occluded.end();
        widget->tick(ctx, isOccluded);
    }
    for (std::size_t i = 0; i < node.childCount(); ++i) {
        if (SceneNode* child = node.childAt(i)) {
            tickSubtree(*child, ctx, occluded);
        }
    }
}

}  // namespace

void Widget::tickTree(const FrameContext& ctx) {
    // 若首帧有布局未决，先执行子树布局更新或直接路由：
    const auto occluded = routePointerEventInTree(*this, ctx);
    tickSubtree(*this, ctx, occluded);
}

}  // namespace kiriyama::scene
