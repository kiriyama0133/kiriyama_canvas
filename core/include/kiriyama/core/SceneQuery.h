#pragma once

// 场景树查询：遍历、按 id / key 定位、包围盒、命中测试。
//
// 遍历一律是前序（父 -> 子 -> 孙），DFS 与 BFS 两种顺序。
// 遍历回调里不要增删节点——正在遍历的 children 容器会被改掉。

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "Scene.h"

namespace kiriyama::core {

// ---------------------------------------------------------------- 遍历

/// 深度优先前序（含 root 自身）。
template <typename Fn>
void forEachDepthFirst(SceneNode& root, Fn&& fn) {
    fn(root);
    for (const auto& child : root.children()) {
        if (child) {
            forEachDepthFirst(*child, fn);
        }
    }
}

template <typename Fn>
void forEachDepthFirst(const SceneNode& root, Fn&& fn) {
    fn(root);
    for (const auto& child : root.children()) {
        if (child) {
            forEachDepthFirst(*child, fn);
        }
    }
}

/// 广度优先（含 root 自身），兄弟之间的顺序与深度优先一致。
template <typename Fn>
void forEachBreadthFirst(SceneNode& root, Fn&& fn) {
    std::vector<SceneNode*> queue{&root};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        SceneNode* node = queue[i];
        fn(*node);
        for (const auto& child : node->children()) {
            if (child) {
                queue.push_back(child.get());
            }
        }
    }
}

template <typename Fn>
void forEachBreadthFirst(const SceneNode& root, Fn&& fn) {
    std::vector<const SceneNode*> queue{&root};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const SceneNode* node = queue[i];
        fn(*node);
        for (const auto& child : node->children()) {
            if (child) {
                queue.push_back(child.get());
            }
        }
    }
}

// ---------------------------------------------------------------- 查找

/// 第一个满足条件的节点（前序），没有返回 nullptr。
template <typename Fn>
SceneNode* findFirst(SceneNode& root, Fn&& predicate) {
    if (predicate(root)) {
        return &root;
    }
    for (const auto& child : root.children()) {
        if (!child) {
            continue;
        }
        if (SceneNode* found = findFirst(*child, predicate)) {
            return found;
        }
    }
    return nullptr;
}

template <typename Fn>
const SceneNode* findFirst(const SceneNode& root, Fn&& predicate) {
    if (predicate(root)) {
        return &root;
    }
    for (const auto& child : root.children()) {
        if (!child) {
            continue;
        }
        if (const SceneNode* found = findFirst(*child, predicate)) {
            return found;
        }
    }
    return nullptr;
}

/// 所有满足条件的节点（前序）。
template <typename Fn>
std::vector<SceneNode*> collectNodes(SceneNode& root, Fn&& predicate) {
    std::vector<SceneNode*> result;
    forEachDepthFirst(root, [&](SceneNode& node) {
        if (predicate(node)) {
            result.push_back(&node);
        }
    });
    return result;
}

template <typename Fn>
std::vector<const SceneNode*> collectNodes(const SceneNode& root, Fn&& predicate) {
    std::vector<const SceneNode*> result;
    forEachDepthFirst(root, [&](const SceneNode& node) {
        if (predicate(node)) {
            result.push_back(&node);
        }
    });
    return result;
}

/// 按节点 id 查（id 是构造时分配的，diff 之后仍然稳定）。
[[nodiscard]] SceneNode* findById(SceneNode& root, std::uint64_t id);
[[nodiscard]] const SceneNode* findById(const SceneNode& root, std::uint64_t id);

/// 按业务 key 查，返回前序第一个；重复 key 请用 collectNodes。
[[nodiscard]] SceneNode* findByKey(SceneNode& root, std::string_view key);
[[nodiscard]] const SceneNode* findByKey(const SceneNode& root, std::string_view key);

[[nodiscard]] std::size_t countNodes(const SceneNode& root);

/// 根节点深度为 0。
[[nodiscard]] int depthOf(const SceneNode& node);

/// 从自身到根（自身在第一位），方便做事件冒泡 / 变换回溯。
[[nodiscard]] std::vector<SceneNode*> ancestorChain(SceneNode& node);
[[nodiscard]] std::vector<const SceneNode*> ancestorChain(const SceneNode& node);

/// 收集所有脏节点，渲染层可以只重画这些子树。
[[nodiscard]] std::vector<SceneNode*> collectDirtyNodes(SceneNode& root);

// ---------------------------------------------------------------- 命中测试

struct SceneHit {
    SceneNode* node{nullptr};
    /// root -> ... -> node，冒泡时从后往前用。
    std::vector<SceneNode*> chain;
    Vec2d localPoint{};
    /// 从根到命中节点的变换，等价于 node->worldTransform()。
    Affine2D worldTransform{};

    [[nodiscard]] explicit operator bool() const noexcept { return node != nullptr; }
};

/// 命中最上层（最后绘制）的可绘制节点。
///
/// Group 只用作剪枝，自身不会作为命中结果返回；不可见节点连同子树一起跳过。
/// tolerance 会放宽细线 / 小点的可点范围。
[[nodiscard]] SceneHit hitTestScene(SceneNode& root, const Vec2d& worldPoint,
                                    double tolerance = 0.0);

/// 从节点向上找第一个显式声明了 cursor 的祖先，返回它的形状；
/// 一路都是 Inherit 时返回 CursorShape::Default。
///
/// 这样"只有可排序的表头是手型"只需要给表头节点写一个 Pointer，
/// 表格正文、容器、根节点全都不用管光标。
[[nodiscard]] CursorShape resolveCursor(const SceneNode& node) noexcept;

}  // namespace kiriyama::core
