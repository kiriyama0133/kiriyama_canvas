#include "kiriyama/render/TooltipHost.h"

#include "kiriyama/core/Geometry.h"

namespace kiriyama::render {

TooltipHost::TooltipHost(App& app)
    : app_(app), renderer_(app.painter(), app.text()) {}

void TooltipHost::setOptions(const core::TooltipOptions& options) {
    renderer_.setOptions(options);
}

void TooltipHost::setStyle(const TooltipStyle& style) {
    renderer_.setStyle(style);
}

void TooltipHost::update(SceneNode& root, const scene::FrameContext& ctx) {
    // FrameContext 的时间是秒，core::TooltipState 收毫秒。
    const double nowMs = ctx.time * 1000.0;

    current_ = scene::findHoveredTooltipTarget(root);
    const std::uint64_t id = current_ != nullptr ? current_->id() : 0;

    // 跟随鼠标时锚点就是指针（2x2 的小方框），气泡像浮层一样跟着光标走；
    // 关掉才用目标自己的锚点（图表组件锚在整块图表上）。
    // id 不变时 TooltipState 不会重置计时，所以指针在同一目标上移动时气泡不会闪。
    core::RectD anchor;
    if (current_ != nullptr) {
        if (followPointer_) {
            const Vec2d pointer = ctx.pointer.position;
            anchor = core::RectD{pointer.x - 1.0, pointer.y - 1.0, 2.0, 2.0};
        } else {
            anchor = current_->tooltipAnchor();
        }
    }

    // 组件可以显式指定贴哪一侧（例如图表要求气泡浮在整张图上方）。
    // 没指定就听全局配置——全局 side 是页面通过 C API 设的。
    if (current_ != nullptr && current_->hasExplicitTooltipSide()) {
        core::TooltipOptions options = renderer_.options();
        options.side = current_->tooltipSide();
        renderer_.setOptions(options);
    }

    // 每帧都喂一次：id 不变时不会重置计时，指针在同一个目标上移动不该一直不弹出。
    state_.hover(id, anchor, nowMs);
    if (ctx.pointer.down) {
        // 按下时挡住目标很烦，core 里由 hideOnPress 决定收不收。
        (void)state_.press(nowMs);
    }
    (void)state_.update(nowMs);

    if (!state_.visible()) {
        return;
    }

    scene::TooltipText text;
    if (!resolveContent(state_.targetId(), text)) {
        return;
    }
    (void)renderer_.draw(state_.anchor(), TooltipContent{text.title, text.body}, app_.canvas(),
                         state_.opacity(nowMs));
}

bool TooltipHost::resolveContent(std::uint64_t nodeId, scene::TooltipText& out) {
    if (resolver_ && resolver_(nodeId, out)) {
        return true;
    }
    // id 对得上才用：树重建之后 current_ 可能已经指向别的节点，
    // 那时 TooltipState 里的 id 还是旧的，取回来会是另一份内容。
    if (current_ != nullptr && current_->id() == nodeId) {
        out = current_->tooltipContent();
        return !out.title.empty() || !out.body.empty();
    }
    return false;
}

}  // namespace kiriyama::render
