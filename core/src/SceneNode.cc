#include "kiriyama/core/SceneNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace kiriyama::core {
namespace {

std::atomic<std::uint64_t> g_nextNodeId{1};

}  // namespace

// ---------------------------------------------------------------- 构造

SceneNode::SceneNode(NodeType type, std::string key)
    : id_(g_nextNodeId.fetch_add(1, std::memory_order_relaxed)),
      type_(type),
      key_(std::move(key)) {}

SceneNode::~SceneNode() = default;

void SceneNode::setType(NodeType type) noexcept {
    if (type_ == type) {
        return;
    }
    type_ = type;
    markDirty();
}

void SceneNode::setKey(std::string key) { key_ = std::move(key); }

void SceneNode::setVisible(bool visible) noexcept {
    if (visible_ == visible) {
        return;
    }
    visible_ = visible;
    markDirty();
}

// ---------------------------------------------------------------- 图元数据

void SceneNode::setFrame(const RectD& frame) noexcept {
    if (frame_ == frame) {
        return;
    }
    frame_ = frame;
    markDirty();
}

void SceneNode::setPoints(std::vector<Vec2d> points) {
    points_ = std::move(points);
    markDirty();
}

void SceneNode::addPoint(const Vec2d& point) {
    points_.push_back(point);
    markDirty();
}

void SceneNode::setText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    markDirty();
}

void SceneNode::setClipRect(const RectD& rect) noexcept {
    if (clipRect_ == rect) {
        return;
    }
    clipRect_ = rect;
    markDirty();
}

void SceneNode::setStyle(const SceneStyle& style) noexcept {
    if (style_ == style) {
        return;
    }
    style_ = style;
    markDirty();
}

SceneStyle& SceneNode::mutableStyle() noexcept {
    markDirty();
    return style_;
}

void SceneNode::setTransform(const Affine2D& transform) noexcept {
    if (transform_ == transform) {
        return;
    }
    transform_ = transform;
    markDirty();
}

void SceneNode::setTranslation(const Vec2d& offset) noexcept {
    // 只改平移分量，保留旋转 / 缩放，避免反复 TRS 重建时的漂移。
    if (transform_.m02 == offset.x && transform_.m12 == offset.y) {
        return;
    }
    transform_.m02 = offset.x;
    transform_.m12 = offset.y;
    markDirty();
}

void SceneNode::translate(const Vec2d& delta) noexcept {
    if (delta.x == 0.0 && delta.y == 0.0) {
        return;
    }
    transform_.m02 += delta.x;
    transform_.m12 += delta.y;
    markDirty();
}

// ---------------------------------------------------------------- 树结构

SceneNode* SceneNode::childAt(std::size_t index) noexcept {
    return index < children_.size() ? children_[index].get() : nullptr;
}

const SceneNode* SceneNode::childAt(std::size_t index) const noexcept {
    return index < children_.size() ? children_[index].get() : nullptr;
}

int SceneNode::indexInParent() const noexcept {
    if (parent_ == nullptr) {
        return -1;
    }
    const auto& siblings = parent_->children_;
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i].get() == this) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

SceneNode* SceneNode::appendChild(std::unique_ptr<SceneNode> child) {
    return insertChild(children_.size(), std::move(child));
}

SceneNode* SceneNode::insertChild(std::size_t index, std::unique_ptr<SceneNode> child) {
    if (!child) {
        return nullptr;
    }
    const std::size_t at = std::min(index, children_.size());
    child->parent_ = this;
    SceneNode* raw = child.get();
    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(at), std::move(child));
    markDirty();
    return raw;
}

std::unique_ptr<SceneNode> SceneNode::removeChild(std::size_t index) {
    if (index >= children_.size()) {
        return nullptr;
    }
    std::unique_ptr<SceneNode> child = std::move(children_[index]);
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
    child->parent_ = nullptr;
    markDirty();
    return child;
}

std::unique_ptr<SceneNode> SceneNode::removeChild(SceneNode* child) {
    if (child == nullptr || child->parent_ != this) {
        return nullptr;
    }
    const auto it = std::find_if(children_.begin(), children_.end(),
                                 [child](const std::unique_ptr<SceneNode>& node) {
                                     return node.get() == child;
                                 });
    if (it == children_.end()) {
        return nullptr;
    }
    std::unique_ptr<SceneNode> owned = std::move(*it);
    children_.erase(it);
    owned->parent_ = nullptr;
    markDirty();
    return owned;
}

void SceneNode::clearChildren() {
    if (children_.empty()) {
        return;
    }
    for (auto& child : children_) {
        child->parent_ = nullptr;
    }
    children_.clear();
    markDirty();
}

std::vector<std::unique_ptr<SceneNode>> SceneNode::takeChildren() {
    // 故意不标脏、不改 parent_：这是 diff 重排的中转状态，
    // 调用方紧接着必须 setChildren() 把（可能是同一个）子节点列表接回来。
    return std::move(children_);
}

void SceneNode::setChildren(std::vector<std::unique_ptr<SceneNode>> children,
                            bool structurallyChanged) {
    for (auto& child : children_) {
        if (child) {
            child->parent_ = nullptr;
        }
    }
    children_ = std::move(children);
    for (auto& child : children_) {
        if (child) {
            child->parent_ = this;
        }
    }
    if (structurallyChanged) {
        markDirty();
    }
}

std::unique_ptr<SceneNode> SceneNode::clone() const {
    auto copy = std::make_unique<SceneNode>(type_, key_);
    copy->name_ = name_;
    copy->visible_ = visible_;
    copy->frame_ = frame_;
    copy->clipRect_ = clipRect_;
    copy->points_ = points_;
    copy->text_ = text_;
    copy->style_ = style_;
    copy->transform_ = transform_;
    copy->dirty_ = true;
    copy->children_.reserve(children_.size());
    for (const auto& child : children_) {
        if (!child) {
            continue;
        }
        auto cloneChild = child->clone();
        cloneChild->parent_ = copy.get();
        copy->children_.push_back(std::move(cloneChild));
    }
    return copy;
}

// ---------------------------------------------------------------- 几何

RectD SceneNode::localBounds() const noexcept {
    switch (type_) {
        case NodeType::Group:
            return {};
        case NodeType::Rect:
        case NodeType::Ellipse:
        case NodeType::Text:
            return frame_;
        case NodeType::Line:
        case NodeType::Polyline:
        case NodeType::Polygon:
            break;
    }

    if (points_.empty()) {
        return {};
    }
    double left = points_.front().x;
    double right = left;
    double top = points_.front().y;
    double bottom = top;
    for (const Vec2d& point : points_) {
        left = std::fmin(left, point.x);
        right = std::fmax(right, point.x);
        top = std::fmin(top, point.y);
        bottom = std::fmax(bottom, point.y);
    }
    return RectD::FromLTRB(left, top, right, bottom);
}

RectD SceneNode::bounds() const noexcept {
    RectD result = localBounds();
    for (const auto& child : children_) {
        if (!child || !child->visible_) {
            continue;
        }
        result = result.united(transformedBounds(child->transform_, child->bounds()));
    }
    return result;
}

Affine2D SceneNode::worldTransform() const noexcept {
    Affine2D matrix = transform_;
    for (const SceneNode* node = parent_; node != nullptr; node = node->parent_) {
        matrix = node->transform_ * matrix;
    }
    return matrix;
}

RectD SceneNode::worldBounds() const noexcept {
    return transformedBounds(worldTransform(), bounds());
}

RectD SceneNode::worldClipRect() const noexcept {
    if (clipRect_.empty()) {
        return {};
    }
    return transformedBounds(worldTransform(), clipRect_);
}

Vec2d SceneNode::toWorld(const Vec2d& localPoint) const noexcept {
    return worldTransform().apply(localPoint);
}

Vec2d SceneNode::toLocal(const Vec2d& worldPoint) const noexcept {
    return worldTransform().inverse().apply(worldPoint);
}

bool SceneNode::containsLocal(const Vec2d& localPoint, double tolerance) const noexcept {
    const double tol = tolerance > 0.0 ? tolerance : 0.0;
    const double halfStroke = style_.hasStroke() ? style_.strokeWidth * 0.5 : 0.0;
    const double strokeSlack = halfStroke + tol;

    switch (type_) {
        case NodeType::Group:
            return bounds().expanded(tol).contains(localPoint);

        case NodeType::Rect: {
            if (frame_.empty()) {
                return false;
            }
            if (style_.hasFill() && frame_.contains(localPoint)) {
                return true;
            }
            return distanceToRectBorder(frame_, localPoint) <= strokeSlack;
        }

        case NodeType::Ellipse: {
            const double rx = frame_.width * 0.5;
            const double ry = frame_.height * 0.5;
            if (!(rx > 0.0) || !(ry > 0.0)) {
                return false;
            }
            const Vec2d center = frame_.center();
            const double nx = (localPoint.x - center.x) / rx;
            const double ny = (localPoint.y - center.y) / ry;
            const double distance = std::sqrt(nx * nx + ny * ny);
            if (style_.hasFill()) {
                return distance <= 1.0;
            }
            // 描边命中：按半径差换算回像素，近似用短半轴
            return std::abs(distance - 1.0) * std::fmin(rx, ry) <= strokeSlack;
        }

        case NodeType::Text:
            return frame_.expanded(tol + halfStroke).contains(localPoint);

        case NodeType::Line: {
            if (points_.size() < 2) {
                return false;
            }
            return distanceToSegment(localPoint, points_[0], points_[1]) <= strokeSlack;
        }

        case NodeType::Polyline:
            if (points_.empty()) {
                return false;
            }
            return distanceToPolyline(localPoint, points_) <= strokeSlack;

        case NodeType::Polygon:
            if (points_.empty()) {
                return false;
            }
            if (style_.hasFill() && polygonContains(points_, localPoint)) {
                return true;
            }
            return distanceToPolyline(localPoint, points_) <= strokeSlack;
    }
    return false;
}

bool SceneNode::containsPoint(const Vec2d& worldPoint, double tolerance) const noexcept {
    return containsLocal(toLocal(worldPoint), tolerance);
}

// ---------------------------------------------------------------- 脏标记

void SceneNode::markSubtreeDirty() noexcept {
    dirty_ = true;
    for (auto& child : children_) {
        if (child) {
            child->markSubtreeDirty();
        }
    }
}

void SceneNode::clearSubtreeDirty() noexcept {
    dirty_ = false;
    for (auto& child : children_) {
        if (child) {
            child->clearSubtreeDirty();
        }
    }
}

bool SceneNode::hasDirtyInSubtree() const noexcept {
    if (dirty_) {
        return true;
    }
    for (const auto& child : children_) {
        if (child && child->hasDirtyInSubtree()) {
            return true;
        }
    }
    return false;
}

}  // namespace kiriyama::core
