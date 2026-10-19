#include "kiriyama/scene/Widget.h"

#include <algorithm>
#include <utility>

#include "kiriyama/math/MathUtils.h"

namespace kiriyama::scene {

namespace {

/// 把本地矩形用仿射变换投到世界坐标，取 AABB。
/// 旋转 / 缩放下也成立，不影响"能不能点中"这个粗判定。
[[nodiscard]] RectD toWorldBounds(const RectD& local, const core::Affine2D& transform) noexcept {
    const Vec2d corners[4] = {
        transform.apply(Vec2d{local.left(), local.top()}),
        transform.apply(Vec2d{local.right(), local.top()}),
        transform.apply(Vec2d{local.right(), local.bottom()}),
        transform.apply(Vec2d{local.left(), local.bottom()}),
    };
    double left = corners[0].x;
    double right = corners[0].x;
    double top = corners[0].y;
    double bottom = corners[0].y;
    for (const Vec2d& corner : corners) {
        left = std::min(left, corner.x);
        right = std::max(right, corner.x);
        top = std::min(top, corner.y);
        bottom = std::max(bottom, corner.y);
    }
    return RectD::FromLTRB(left, top, right, bottom);
}

void tickSubtree(SceneNode& node, const FrameContext& ctx);

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

// ---------------------------------------------------------------- 命中区

RectD Widget::localHitArea() const noexcept {
    const RectD& box = frame();
    if (!box.empty()) {
        return box;
    }
    // 没设 frame 时退化成"包住子树"：Group 的 localBounds 恒为空，直接用它反而永远点不中。
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
    return hitShape(worldPoint);
}

// ---------------------------------------------------------------- 驱动

void Widget::updatePresence() {
    inside_ = hitTest(pointer_);
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

void Widget::tick(const FrameContext& ctx) {
    const Vec2d position = ctx.pointer.position;
    pointerDelta_ = hasPointer_ ? Vec2d{position.x - pointer_.x, position.y - pointer_.y} : Vec2d{};
    pointer_ = position;
    hasPointer_ = true;

    updatePresence();

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
    }
    down_ = down;

    // 松手会顺带解除捕获，此时指针可能已经不在组件上了——再算一次才能补上 leave。
    updatePresence();

    onUpdate(ctx);
}

void Widget::tickTree(const FrameContext& ctx) {
    tickSubtree(*this, ctx);
}

namespace {

void tickSubtree(SceneNode& node, const FrameContext& ctx) {
    if (auto* widget = dynamic_cast<Widget*>(&node)) {
        widget->tick(ctx);
    }
    for (std::size_t i = 0; i < node.childCount(); ++i) {
        if (SceneNode* child = node.childAt(i)) {
            tickSubtree(*child, ctx);
        }
    }
}

}  // namespace

}  // namespace kiriyama::scene
