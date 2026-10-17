#include "kiriyama/core/SceneQuery.h"

#include <algorithm>

namespace kiriyama::core {
namespace {

SceneNode* findByIdImpl(SceneNode& node, std::uint64_t id) {
    if (node.id() == id) {
        return &node;
    }
    for (const auto& child : node.children()) {
        if (!child) {
            continue;
        }
        if (SceneNode* found = findByIdImpl(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

SceneNode* findByKeyImpl(SceneNode& node, std::string_view key) {
    if (!key.empty() && node.key() == key) {
        return &node;
    }
    for (const auto& child : node.children()) {
        if (!child) {
            continue;
        }
        if (SceneNode* found = findByKeyImpl(*child, key)) {
            return found;
        }
    }
    return nullptr;
}

std::size_t countNodesImpl(const SceneNode& node) {
    std::size_t total = 1;
    for (const auto& child : node.children()) {
        if (child) {
            total += countNodesImpl(*child);
        }
    }
    return total;
}

void collectDirtyImpl(SceneNode& node, std::vector<SceneNode*>& out) {
    if (node.dirty()) {
        out.push_back(&node);
    }
    for (const auto& child : node.children()) {
        if (child) {
            collectDirtyImpl(*child, out);
        }
    }
}

SceneNode* hitNode(SceneNode& node, const Vec2d& worldPoint, double tolerance) {
    if (!node.visible()) {
        return nullptr;
    }

    const Vec2d local = node.toLocal(worldPoint);
    // 剪枝：包围盒（含子树）都不含这个点，整棵跳过。
    // 注意不能因为 bounds.empty() 就提前退出——水平线 / 细条的包围盒高度为 0，
    // 它们恰恰是需要 tolerance 才点得中的那一类。
    if (!node.bounds().expanded(tolerance).contains(local)) {
        return nullptr;
    }

    // 子节点后绘制，因此从后往前找，第一个命中的就是最上层。
    for (std::size_t i = node.childCount(); i-- > 0;) {
        SceneNode* child = node.childAt(i);
        if (!child) {
            continue;
        }
        if (SceneNode* hit = hitNode(*child, worldPoint, tolerance)) {
            return hit;
        }
    }

    // Group 只做容器，自身不出现在命中结果里。
    if (node.type() != NodeType::Group && node.containsLocal(local, tolerance)) {
        return &node;
    }
    return nullptr;
}

}  // namespace

SceneNode* findById(SceneNode& root, std::uint64_t id) { return findByIdImpl(root, id); }

const SceneNode* findById(const SceneNode& root, std::uint64_t id) {
    return findByIdImpl(const_cast<SceneNode&>(root), id);
}

SceneNode* findByKey(SceneNode& root, std::string_view key) { return findByKeyImpl(root, key); }

const SceneNode* findByKey(const SceneNode& root, std::string_view key) {
    return findByKeyImpl(const_cast<SceneNode&>(root), key);
}

std::size_t countNodes(const SceneNode& root) { return countNodesImpl(root); }

int depthOf(const SceneNode& node) {
    int depth = 0;
    for (const SceneNode* parent = node.parent(); parent != nullptr; parent = parent->parent()) {
        ++depth;
    }
    return depth;
}

std::vector<SceneNode*> ancestorChain(SceneNode& node) {
    std::vector<SceneNode*> chain;
    for (SceneNode* current = &node; current != nullptr; current = current->parent()) {
        chain.push_back(current);
    }
    return chain;
}

std::vector<const SceneNode*> ancestorChain(const SceneNode& node) {
    std::vector<const SceneNode*> chain;
    for (const SceneNode* current = &node; current != nullptr; current = current->parent()) {
        chain.push_back(current);
    }
    return chain;
}

std::vector<SceneNode*> collectDirtyNodes(SceneNode& root) {
    std::vector<SceneNode*> dirty;
    collectDirtyImpl(root, dirty);
    return dirty;
}

SceneHit hitTestScene(SceneNode& root, const Vec2d& worldPoint, double tolerance) {
    SceneHit result;
    SceneNode* hit = hitNode(root, worldPoint, tolerance);
    if (hit == nullptr) {
        return result;
    }

    result.node = hit;
    result.worldTransform = hit->worldTransform();
    result.localPoint = result.worldTransform.inverse().apply(worldPoint);

    for (SceneNode* current = hit; current != nullptr; current = current->parent()) {
        result.chain.push_back(current);
    }
    std::reverse(result.chain.begin(), result.chain.end());
    return result;
}

CursorShape resolveCursor(const SceneNode& node) noexcept {
    for (const SceneNode* current = &node; current != nullptr; current = current->parent()) {
        const CursorShape shape = current->style().cursor;
        if (shape != CursorShape::Inherit) {
            return shape;
        }
    }
    return CursorShape::Default;
}

}  // namespace kiriyama::core
