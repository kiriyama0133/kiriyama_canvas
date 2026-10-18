#pragma once

// 场景树对账（类似 Vue 的 patch / virtual DOM diff）。
//
// 用法：每帧/每次数据更新都从数据重新构造一棵"描述树"，然后
//     auto diff = reconcileScene(sceneRoot, descriptionRoot);
// 让 sceneRoot 就地变成描述树的样子：
//   - key 相同且类型相同的节点直接复用（id 不变），只把变化的属性写进去
//   - 只有 key 相同才能保住节点身份；没有 key 的节点按位置 + 类型配对
//   - 新出现的节点克隆整棵子树插入，消失的整棵子树摘掉
//   - 顺序变化直接重排，不重建节点
// 返回的 changed 就是需要重绘的起点，其余节点可以按 hasDirtyInSubtree() 整棵跳过。
//
// 两个约定：
//   1. 根节点不能被替换，类型不同时直接改类型（id 保留）；子节点类型不同按"删旧插新"处理。
//   2. 描述树在调用后仍然归调用方所有，可以丢掉了；target 才是被维护的那棵。

#include <cstddef>
#include <vector>

#include "Scene.h"

namespace kiriyama::core {

struct SceneDiffStats {
    std::size_t inserted{0};  ///< 新建节点总数（含子树）
    std::size_t removed{0};   ///< 摘掉的节点总数（含子树）
    std::size_t updated{0};   ///< 属性被改写的节点数
    std::size_t moved{0};     ///< 兄弟间重排的最小移动次数
    std::size_t reused{0};    ///< 复用的节点数（含未变化的）

    [[nodiscard]] bool changed() const noexcept {
        return inserted > 0 || removed > 0 || updated > 0 || moved > 0;
    }
};

struct SceneDiffResult {
    SceneDiffStats stats;
    /// 新树中需要重绘的节点（属性变化的节点 + 新插入子树的根）。
    std::vector<SceneNode*> changed;
};

/// 把 target 就地更新成 source 的样子，返回统计与变化节点（保留 source，新子树会 clone）。
[[nodiscard]] SceneDiffResult reconcileScene(SceneNode& target, const SceneNode& source);

/// 消费型对账：直接接管 source 中的新子树与顶点缓冲，消除 clone 与二次拷贝。
[[nodiscard]] SceneDiffResult reconcileScene(SceneNode& target, SceneNode&& source);

/// 只比属性（图元数据 / 样式 / 变换 / 可见性），不比子节点和 key。
[[nodiscard]] bool sameProperties(const SceneNode& lhs, const SceneNode& rhs) noexcept;

}  // namespace kiriyama::core
