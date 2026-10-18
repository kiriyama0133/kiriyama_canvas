#include "kiriyama/scene/TooltipTarget.h"

#include "kiriyama/core/SceneQuery.h"

namespace kiriyama::scene {

TooltipTarget::TooltipTarget(std::string key) : Widget(std::move(key)) {}

void TooltipTarget::setTooltipContent(std::string title, std::string body) {
    tipTitle_ = std::move(title);
    tipBody_ = std::move(body);
}

TooltipText TooltipTarget::tooltipContent() const {
    return TooltipText{tipTitle_, tipBody_};
}

bool TooltipTarget::hasTooltipContent() const {
    return !tipTitle_.empty() || !tipBody_.empty();
}

core::RectD TooltipTarget::tooltipAnchor() const {
    return hitBounds();
}

TooltipTarget* findHoveredTooltipTarget(SceneNode& root) noexcept {
    // 两档候选：focus 压过 hover（和 core::TooltipState 的规则一致），
    // 同一档内部按绘制顺序取后画的 —— 视觉上压在上面的那个。
    TooltipTarget* focused = nullptr;
    TooltipTarget* hovered = nullptr;

    // 自己递归而不是 forEachDepthFirst：不可见的节点要连子树一起剪掉，
    // 而通用遍历没有剪枝这个概念（藏起来的组里也可能有"命中"的子组件）。
    const auto walk = [&focused, &hovered](auto&& self, SceneNode& node) -> void {
        if (!node.visible()) {
            return;
        }
        if (auto* target = dynamic_cast<TooltipTarget*>(&node)) {
            // dynamic_cast 而不是静态分支：树里允许夹普通 Group，
            // 命中判定已经在 tick 里做完了，这里只负责"谁该弹"。
            if (target->tooltipEnabled() && target->hasTooltipContent()) {
                if (target->tooltipFocused()) {
                    focused = target;
                } else if (target->hovered()) {
                    hovered = target;
                }
            }
        }
        for (const auto& child : node.children()) {
            if (child) {
                self(self, *child);
            }
        }
    };
    walk(walk, root);

    return focused != nullptr ? focused : hovered;
}

}  // namespace kiriyama::scene
