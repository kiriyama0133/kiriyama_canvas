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
    const core::RectD anchor =
        current_ != nullptr ? current_->tooltipAnchor() : core::RectD{};

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
