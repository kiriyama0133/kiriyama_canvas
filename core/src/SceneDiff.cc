#include "kiriyama/core/SceneDiff.h"

#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kiriyama/core/SceneQuery.h"

namespace kiriyama::core {
namespace {

constexpr std::size_t kNoMatch = std::numeric_limits<std::size_t>::max();

[[nodiscard]] bool samePoints(std::span<const Vec2d> lhs, std::span<const Vec2d> rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!(lhs[i] == rhs[i])) {
            return false;
        }
    }
    return true;
}

/// 属性写入：每个 setter 自己会判等，所以这里可以无脑写一遍。
void copyProperties(SceneNode& target, const SceneNode& source) {
    target.setType(source.type());
    target.setVisible(source.visible());
    target.setFrame(source.frame());
    if (!samePoints(target.points(), source.points())) {
        target.setPoints(std::vector<Vec2d>(source.points().begin(), source.points().end()));
    }
    target.setText(source.text());
    target.setStyle(source.style());
    target.setTransform(source.transform());
}

/// 把旧顺序变成新顺序所需的最少移动次数 = 匹配数 - 最长递增子序列长度。
[[nodiscard]] std::size_t minimumMoves(const std::vector<std::size_t>& oldIndices) {
    if (oldIndices.empty()) {
        return 0;
    }
    std::vector<std::size_t> tails;
    tails.reserve(oldIndices.size());
    for (const std::size_t value : oldIndices) {
        const auto it = std::lower_bound(tails.begin(), tails.end(), value);
        if (it == tails.end()) {
            tails.push_back(value);
        } else {
            *it = value;
        }
    }
    return oldIndices.size() - tails.size();
}

void reconcileNode(SceneNode& target, const SceneNode& source, SceneDiffResult& out);
void reconcileChildren(SceneNode& target, const SceneNode& source, SceneDiffResult& out);

void reconcileNode(SceneNode& target, const SceneNode& source, SceneDiffResult& out) {
    // key / name 不是可视属性，静默同步即可。
    if (target.key() != source.key()) {
        target.setKey(source.key());
    }
    if (target.name() != source.name()) {
        target.setName(source.name());
    }

    if (!sameProperties(target, source)) {
        copyProperties(target, source);
        target.markDirty();
        ++out.stats.updated;
        out.changed.push_back(&target);
    }

    if (source.childCount() > 0 || target.childCount() > 0) {
        reconcileChildren(target, source, out);
    }
}

void reconcileChildren(SceneNode& target, const SceneNode& source, SceneDiffResult& out) {
    std::vector<std::unique_ptr<SceneNode>> oldChildren = target.takeChildren();
    const std::size_t oldCount = oldChildren.size();

    // key -> 旧下标。重复 key 只认第一个，剩下的走插入路径。
    std::unordered_map<std::string_view, std::size_t> keyIndex;
    keyIndex.reserve(oldCount);
    for (std::size_t i = 0; i < oldCount; ++i) {
        if (!oldChildren[i]) {
            continue;
        }
        const std::string& key = oldChildren[i]->key();
        if (!key.empty()) {
            keyIndex.emplace(key, i);
        }
    }

    std::vector<char> used(oldCount, 0);
    std::vector<std::unique_ptr<SceneNode>> next;
    next.reserve(source.childCount());
    std::vector<std::size_t> matchedOldIndices;
    matchedOldIndices.reserve(source.childCount());
    std::size_t unkeyedCursor = 0;
    std::size_t insertedDelta = 0;
    std::size_t removedDelta = 0;

    for (std::size_t i = 0; i < source.childCount(); ++i) {
        const SceneNode* src = source.childAt(i);
        if (src == nullptr) {
            continue;
        }

        std::size_t match = kNoMatch;
        if (!src->key().empty()) {
            const auto it = keyIndex.find(src->key());
            if (it != keyIndex.end()) {
                const std::size_t candidate = it->second;
                // 类型不同视作不同节点（Vue 里 tag 变了也是换节点）
                if (!used[candidate] && oldChildren[candidate] &&
                    oldChildren[candidate]->type() == src->type()) {
                    match = candidate;
                }
            }
        } else {
            // 无 key：按位置 + 类型兜底配对，从游标往后找
            for (std::size_t j = unkeyedCursor; j < oldCount; ++j) {
                if (used[j] || !oldChildren[j]) {
                    continue;
                }
                if (!oldChildren[j]->key().empty()) {
                    continue;
                }
                if (oldChildren[j]->type() != src->type()) {
                    continue;
                }
                match = j;
                unkeyedCursor = j + 1;
                break;
            }
        }

        if (match != kNoMatch) {
            used[match] = 1;
            ++out.stats.reused;
            reconcileNode(*oldChildren[match], *src, out);
            if (!src->key().empty()) {
                matchedOldIndices.push_back(match);
            }
            next.push_back(std::move(oldChildren[match]));
            continue;
        }

        // 新节点：整棵克隆进来，新子树全部需要绘制。
        std::unique_ptr<SceneNode> cloned = src->clone();
        insertedDelta += countNodes(*cloned);
        cloned->markSubtreeDirty();
        out.changed.push_back(cloned.get());
        next.push_back(std::move(cloned));
    }

    for (std::size_t i = 0; i < oldCount; ++i) {
        if (!used[i] && oldChildren[i]) {
            removedDelta += countNodes(*oldChildren[i]);
        }
    }

    const std::size_t moves = minimumMoves(matchedOldIndices);
    out.stats.inserted += insertedDelta;
    out.stats.removed += removedDelta;
    out.stats.moved += moves;

    const bool structuralChange = insertedDelta > 0 || removedDelta > 0 || moves > 0;
    // 结构没变时不要标脏：整棵树可能一次更新都不需要重绘。
    target.setChildren(std::move(next), structuralChange);
}

}  // namespace

bool sameProperties(const SceneNode& lhs, const SceneNode& rhs) noexcept {
    if (lhs.type() != rhs.type() || lhs.visible() != rhs.visible()) {
        return false;
    }
    if (!(lhs.frame() == rhs.frame())) {
        return false;
    }
    if (!(lhs.style() == rhs.style())) {
        return false;
    }
    if (!(lhs.transform() == rhs.transform())) {
        return false;
    }
    if (lhs.text() != rhs.text()) {
        return false;
    }
    return samePoints(lhs.points(), rhs.points());
}

SceneDiffResult reconcileScene(SceneNode& target, const SceneNode& source) {
    SceneDiffResult result;
    // 根节点不能被替换：类型不同时由 copyProperties 就地改类型（id 保留）。
    reconcileNode(target, source, result);
    return result;
}

}  // namespace kiriyama::core
