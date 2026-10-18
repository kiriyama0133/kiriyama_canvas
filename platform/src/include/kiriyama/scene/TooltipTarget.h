#pragma once

// 会弹气泡的组件基类：Widget + 气泡内容。
//
// 这一层解决的是"每个交互组件都要写一遍 hover→气泡"的重复：
//   悬停判定、指针进出、按下收起，全在 Widget 里；
//   内容从哪来（静态串 / 按当前数据算 / 外部注入），在这里留出口子。
//
// ── 关键决定：气泡**不归组件所有** ──────────────────────────────
// 每个组件各自持有一个 core::TooltipState + 一个渲染器，看上去封装更"彻底"，
// 但气泡在语义上是**互斥**的：同一时刻只能有一个。让组件各持一份，就会出现
//   · 悬停 A 的时候 B 的淡出还没走完 → 两个气泡同时挂在屏幕上，互相压；
//   · 每个组件都要复制一份 hideDelay / showDelay 的调参经验；
//   · z 序没有全局概念，后画的不一定盖得住先画的。
// 所以拆成两半：
//   TooltipTarget（本类，scene 层，纯逻辑）  —— 只带"内容"，不带气泡；
//   render::TooltipHost（一页一个）          —— 持有唯一的气泡状态机与渲染器。
// 组件因此依然只管自己的事：setTooltipContent("CHG", "+1.24%") 就完了。
//
// 定制入口（两个都在，按侵入性从低到高）：
//   1. 覆盖 tooltipContent()      —— 按当前数据生成（占比、实时值、单位换算）
//   2. Host::setResolver(...)     —— 完全接管内容来源，气泡连节点都不是
//                                 （例如图表 series 的气泡、跨行汇总）
//
// 和 Widget 一样：不 include 任何 SDL 头，能在 node 里跑单元测试。

#include <string>

#include "kiriyama/core/Tooltip.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

/// 气泡要显示的文字。用 std::string 而不是 string_view：内容可能这一帧才算出来。
struct TooltipText {
    std::string title;
    std::string body;
};

class TooltipTarget : public Widget {
public:
    explicit TooltipTarget(std::string key = {});

    // ------------------------------------------------------------ 内容

    /// 静态内容，最省事的用法。空标题 / 空正文都合法（只有一个就不画另一行）。
    void setTooltipContent(std::string title, std::string body);

    /// 定制入口一：覆盖它就能按当前状态生成内容。
    /// 默认返回 setTooltipContent 设进去的静态串。
    /// 典型用法：饼图扇区在这里算 `label + 占比`，切到 hover 态时数字已经变了。
    [[nodiscard]] virtual TooltipText tooltipContent() const;

    /// 没有内容的组件不该弹气泡（也会被 findHoveredTooltipTarget 跳过）。
    [[nodiscard]] virtual bool hasTooltipContent() const;

    // ------------------------------------------------------------ 开关

    void setTooltipEnabled(bool enabled) noexcept { tooltipEnabled_ = enabled; }
    [[nodiscard]] bool tooltipEnabled() const noexcept { return tooltipEnabled_; }

    /// 键盘焦点。焦点优先级高于悬停（和 core::TooltipState 的规则一致），
    /// 所以一个组件被 focus 时，即使指针不在它身上也该弹。
    void setTooltipFocused(bool focused) noexcept { tooltipFocused_ = focused; }
    [[nodiscard]] bool tooltipFocused() const noexcept { return tooltipFocused_; }

    // ------------------------------------------------------------ 外观参数

    /// 想贴在哪一侧。放不下时 core::computeTooltipLayout 会自己翻面。
    /// 显式设过之后，TooltipHost 会用它覆盖全局配置（没设过就听全局的）。
    void setTooltipSide(core::TooltipSide side) noexcept {
        tooltipSide_ = side;
        tooltipSideExplicit_ = true;
    }
    [[nodiscard]] core::TooltipSide tooltipSide() const noexcept { return tooltipSide_; }
    /// 是否显式设置过 side——区分"组件要贴这边"和"没意见，听全局"。
    [[nodiscard]] bool hasExplicitTooltipSide() const noexcept { return tooltipSideExplicit_; }

    /// 气泡的锚点矩形（世界坐标）。默认是自身命中区——
    /// 锚点跟着组件走，滚动 / 重排时气泡才不会飘。
    /// 定制入口：饼图扇区可以把锚点收到扇区中点，而不是整个 bounding box。
    [[nodiscard]] virtual core::RectD tooltipAnchor() const;

protected:
    /// 静态内容。protected 而不是 private：派生组件（如 PieSlice）要在自己的
    /// tooltipContent() 里先问它有没有——"设了静态就用静态，否则按数据生成"。
    std::string tipTitle_;
    std::string tipBody_;

private:
    bool tooltipEnabled_{true};
    bool tooltipFocused_{false};
    core::TooltipSide tooltipSide_{core::TooltipSide::Above};
    bool tooltipSideExplicit_{false};
};

/// 子树里"当前该弹气泡"的那个目标；没有则返回 nullptr。
///
/// 取的是**最后被访问到的**那一个：遍历是前序，兄弟按绘制顺序走，
/// 所以重叠的命中区里胜出的是后画的（视觉上在上面那个），和视觉层级一致。
/// 前提是 root 已经 tick 过（hovered() 是 tick 时算出来的）。
[[nodiscard]] TooltipTarget* findHoveredTooltipTarget(SceneNode& root) noexcept;

}  // namespace kiriyama::scene
