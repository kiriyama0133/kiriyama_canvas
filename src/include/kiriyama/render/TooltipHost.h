#pragma once

// 气泡宿主：一页一个，持有唯一的 core::TooltipState + TooltipRenderer。
//
// 为什么不是每个组件各持一份：气泡语义上互斥，同一时刻只能有一个。
// 分开放会出现"悬停 A 时 B 的淡出还没完，两个气泡互相压"，而且每个组件都要
// 复制一份延迟调参。所以状态机和渲染器收在这里，组件（scene::TooltipTarget）
// 只提供内容——这就是 TooltipTarget.h 顶部写的那条决定。
//
// 用法（每帧一次，放在所有内容画完之后）：
//
//   render::TooltipHost tip(app);                 // 需要 app.painter() / app.text()
//   tip.setOptions({.side = core::TooltipSide::Above});
//   tip.setStyle({.maxWidth = 220});
//
//   // 帧末
//   root->tickTree(render::frameContext(app));     // 先让 Widget 们算好悬停
//   tip.update(root, render::frameContext(app));   // 再弹气泡
//
// 定制入口：setResolver() 完全接管内容来源，气泡的目标连节点都不是
// （图表 series 汇总、跨行计算这类）。
//
// 和 TooltipRenderer 一样：定位逻辑全在 core，这里只负责"找谁 + 喂状态 + 画"。

#include <cstdint>
#include <functional>

#include "kiriyama/render/App.h"
#include "kiriyama/render/TooltipRenderer.h"
#include "kiriyama/scene/TooltipTarget.h"

namespace kiriyama::render {

using core::SceneNode;
using core::Vec2d;

class TooltipHost {
public:
    explicit TooltipHost(App& app);

    TooltipHost(const TooltipHost&) = delete;
    TooltipHost& operator=(const TooltipHost&) = delete;

    // ------------------------------------------------------------ 配置

    void setOptions(const core::TooltipOptions& options);
    void setStyle(const TooltipStyle& style);
    TooltipRenderer& renderer() noexcept { return renderer_; }

    /// 定制入口二：给定节点 id，往里写 title / body，返回 false 表示"我不会"。
    /// 设了之后优先问它，问不到再回落到节点自己的 tooltipContent()。
    using Resolver = std::function<bool(std::uint64_t nodeId, scene::TooltipText& out)>;
    void setResolver(Resolver resolver) { resolver_ = std::move(resolver); }

    // ------------------------------------------------------------ 每帧

    /// root 是场景树根（里面可以嵌 Widget）。会自己找当前该弹气泡的目标。
    /// 放在帧末调用：要盖在所有内容上面。
    void update(SceneNode& root, const scene::FrameContext& ctx);

    // ------------------------------------------------------------ 查询

    [[nodiscard]] core::TooltipState& state() noexcept { return state_; }
    [[nodiscard]] const core::TooltipState& state() const noexcept { return state_; }

    /// 立即收起（切场景、开始拖拽、数据刷新）。
    void hide() { (void)state_.hide(); }

private:
    /// 把当前可见目标的内容取出来。取不到返回 false。
    [[nodiscard]] bool resolveContent(std::uint64_t nodeId, scene::TooltipText& out);

    App& app_;
    core::TooltipState state_;
    TooltipRenderer renderer_;
    Resolver resolver_;
    /// 本帧找到的悬停目标。用来把 TooltipState 的 id 翻译回内容。
    scene::TooltipTarget* current_{nullptr};
};

}  // namespace kiriyama::render
