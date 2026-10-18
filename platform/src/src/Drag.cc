#include "kiriyama/scene/Drag.h"

#include <algorithm>
#include <utility>

#include "kiriyama/math/MathUtils.h"

namespace kiriyama::scene {

namespace {

[[nodiscard]] double clampAxis(double value, double low, double high) noexcept {
    // 可用区间比节点还小时（low > high），贴住起始边，让它溢出到另一侧。
    return high >= low ? std::clamp(value, low, high) : low;
}

[[nodiscard]] bool allowsX(DragAxis axis) noexcept {
    return axis == DragAxis::Both || axis == DragAxis::X;
}

[[nodiscard]] bool allowsY(DragAxis axis) noexcept {
    return axis == DragAxis::Both || axis == DragAxis::Y;
}

}  // namespace

Drag::Drag(std::string key) : Widget(std::move(key)) {}

Drag& Drag::setOptions(const DragOptions& options) noexcept {
    options_ = options;
    return *this;
}

Drag& Drag::onBegin(Handler handler) {
    onBegin_ = std::move(handler);
    return *this;
}

Drag& Drag::onDrag(Handler handler) {
    onDrag_ = std::move(handler);
    return *this;
}

Drag& Drag::onEnd(Handler handler) {
    onEnd_ = std::move(handler);
    return *this;
}

Vec2d Drag::translation() const noexcept {
    const auto& transform = this->transform();
    return Vec2d{transform.m02, transform.m12};
}

Vec2d Drag::toParent(const Vec2d& world) const noexcept {
    const SceneNode* ancestor = parent();
    if (ancestor == nullptr) {
        return world;  // 没有父节点，世界坐标就是父坐标
    }
    return ancestor->worldTransform().inverse().apply(world);
}

// ---------------------------------------------------------------- 指针驱动

void Drag::onPress() {
    // 基类已经判定过"按下点在命中区内"，这里直接接管。
    if (!active_) {
        begin(pointerPosition());
    }
}

void Drag::onUpdate(const FrameContext& ctx) {
    if (!active_) {
        return;
    }
    if (ctx.pointer.down) {
        move(ctx.pointer.position);
    } else {
        end(ctx.pointer.position);
    }
}

// ---------------------------------------------------------------- 拖拽

bool Drag::begin(const Vec2d& worldPointer) {
    // 重复按下不叠状态：直接当作新的一次拖拽从头开始。
    if (active_) {
        cancel();
    }
    if (!hitTest(worldPointer)) {
        return false;
    }

    grab_ = toParent(worldPointer);
    startTranslation_ = translation();
    delta_ = Vec2d{};
    active_ = true;
    dragging_ = false;
    // 拖出父容器也要继续收移动事件，否则指针一离开边界就断在半路。
    capturePointer(true);

    if (onBegin_) {
        onBegin_(*this);
    }
    return true;
}

Vec2d Drag::clampTranslation(const Vec2d& target) const noexcept {
    if (!options_.bounds.has_value()) {
        return target;
    }

    const RectD& bounds = *options_.bounds;
    const RectD local = localHitArea();  // 本地命中区，含自身 frame
    Vec2d out = target;

    if (allowsX(options_.axis)) {
        out.x = clampAxis(out.x, bounds.left() - local.x, bounds.right() - local.width - local.x);
    }
    if (allowsY(options_.axis)) {
        out.y = clampAxis(out.y, bounds.top() - local.y, bounds.bottom() - local.height - local.y);
    }
    return out;
}

bool Drag::move(const Vec2d& worldPointer) {
    if (!active_) {
        return false;
    }

    const Vec2d current = toParent(worldPointer);
    Vec2d raw{current.x - grab_.x, current.y - grab_.y};

    if (!allowsX(options_.axis)) {
        raw.x = 0.0;
    }
    if (!allowsY(options_.axis)) {
        raw.y = 0.0;
    }

    // 阈值之内只算"按住了"，不算"在拖"——避免点击时的手抖把元素挪走。
    if (!dragging_) {
        if (raw.length() < options_.threshold) {
            return false;
        }
        dragging_ = true;
    }

    const Vec2d target =
        clampTranslation(Vec2d{startTranslation_.x + raw.x, startTranslation_.y + raw.y});
    delta_ = Vec2d{target.x - startTranslation_.x, target.y - startTranslation_.y};

    if (!options_.moveNode) {
        if (onDrag_) {
            onDrag_(*this);
        }
        return true;
    }

    const auto& transform = this->transform();
    if (math::nearlyEqual(transform.m02, target.x) && math::nearlyEqual(transform.m12, target.y)) {
        return false;
    }
    setTranslation(target);
    if (onDrag_) {
        onDrag_(*this);
    }
    return true;
}

bool Drag::end(const Vec2d& worldPointer) {
    if (!active_) {
        return false;
    }
    move(worldPointer);  // 补上最后一小段，免得抬起点丢了

    const bool wasDragging = dragging_;
    active_ = false;
    dragging_ = false;
    capturePointer(false);
    if (onEnd_) {
        onEnd_(*this);
    }
    return wasDragging;
}

void Drag::cancel() noexcept {
    active_ = false;
    dragging_ = false;
    delta_ = Vec2d{};
    capturePointer(false);
}

}  // namespace kiriyama::scene
