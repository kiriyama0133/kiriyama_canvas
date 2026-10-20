#include "kiriyama/scene/ScrollWidget.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "kiriyama/core/SceneDiff.h"

namespace kiriyama::scene {

ScrollWidget::ScrollWidget(std::string key) : Widget(std::move(key)) {
    // 1. 底层：容器可选背景色节点
    auto bg = std::make_unique<core::SceneNode>(core::NodeType::Rect, "scroll.bg");
    bg->setVisible(false);
    bg_ = bg.get();
    appendChild(std::move(bg));

    // 2. 中层：带 clipRect 的 Drag 视口组 -> 承载用户子组件的 content_ 平移组
    auto viewport = std::make_unique<Drag>("scroll.viewport");
    viewport->setOptions(DragOptions{
        .axis = DragAxis::None,
        .bounds = std::nullopt,
        .threshold = 6.0,
        .moveNode = false,
    });
    viewport->onBegin([this](const Drag&) { dragStartScroll_ = scrollOffset_; });
    viewport->onDrag([this](const Drag& d) {
        setScrollOffset({dragStartScroll_.x - d.delta().x, dragStartScroll_.y - d.delta().y});
    });

    auto content = std::make_unique<core::SceneNode>(core::NodeType::Group, "scroll.content");
    content_ = content.get();
    viewport->appendChild(std::move(content));
    viewport_ = viewport.get();
    appendChild(std::move(viewport));

    // 3. 顶层：chrome_ 装饰组，预先挂载可点击的轨道 Widget 与可拖拽的滑块 Drag 节点。
    //    由于它们的 key 与 type (NodeType::Rect) 和 core::buildScrollBars 完全一致，
    //    reconcileScene(*chrome_, *desc) 会原地复用这些实例，只同步它们的 frame、可见性与样式！
    auto chrome = std::make_unique<core::SceneNode>(core::NodeType::Group, "scroll.chrome");

    auto vTrack = std::make_unique<Widget>("scroll.v.track");
    vTrack->setType(core::NodeType::Rect);
    vTrack->setStopPointerPropagation(true);
    vTrack_ = vTrack.get();
    vTrack_->onClick([this]() {
        if (layout_.vertical.scrollable && !vThumb_->pointerInside() && !vThumb_->active()) {
            const core::Vec2d local = toLocal(vTrack_->pointerPosition());
            setScrollY(layout_.vertical.scrollForTrackPosition(local.y, true));
        }
    });

    auto vThumb = std::make_unique<Drag>("scroll.v.thumb");
    vThumb->setType(core::NodeType::Rect);
    vThumb->setStopPointerPropagation(true);
    vThumb->setOptions(DragOptions{.axis = DragAxis::Y, .moveNode = false});
    vThumb->onBegin([this](const Drag&) {
        dragStartScroll_.y = scrollOffset_.y;
        lastVisualState_ = currentVisualState();
        core::updateScrollBars(*chrome_, layout_, frame().size(), barStyle_, lastVisualState_);
    });
    vThumb->onDrag([this](const Drag& d) {
        setScrollY(dragStartScroll_.y +
                   layout_.vertical.scrollDeltaFromThumbDelta(d.delta().y, true));
    });
    vThumb->onEnd([this](const Drag&) {
        lastVisualState_ = currentVisualState();
        core::updateScrollBars(*chrome_, layout_, frame().size(), barStyle_, lastVisualState_);
    });
    vThumb_ = vThumb.get();

    auto hTrack = std::make_unique<Widget>("scroll.h.track");
    hTrack->setType(core::NodeType::Rect);
    hTrack->setStopPointerPropagation(true);
    hTrack_ = hTrack.get();
    hTrack_->onClick([this]() {
        if (layout_.horizontal.scrollable && !hThumb_->pointerInside() && !hThumb_->active()) {
            const core::Vec2d local = toLocal(hTrack_->pointerPosition());
            setScrollX(layout_.horizontal.scrollForTrackPosition(local.x, false));
        }
    });

    auto hThumb = std::make_unique<Drag>("scroll.h.thumb");
    hThumb->setType(core::NodeType::Rect);
    hThumb->setStopPointerPropagation(true);
    hThumb->setOptions(DragOptions{.axis = DragAxis::X, .moveNode = false});
    hThumb->onBegin([this](const Drag&) {
        dragStartScroll_.x = scrollOffset_.x;
        lastVisualState_ = currentVisualState();
        core::updateScrollBars(*chrome_, layout_, frame().size(), barStyle_, lastVisualState_);
    });
    hThumb->onDrag([this](const Drag& d) {
        setScrollX(dragStartScroll_.x +
                   layout_.horizontal.scrollDeltaFromThumbDelta(d.delta().x, false));
    });
    hThumb->onEnd([this](const Drag&) {
        lastVisualState_ = currentVisualState();
        core::updateScrollBars(*chrome_, layout_, frame().size(), barStyle_, lastVisualState_);
    });
    hThumb_ = hThumb.get();

    chrome->appendChild(std::move(vTrack));
    chrome->appendChild(std::move(vThumb));
    chrome->appendChild(std::move(hTrack));
    chrome->appendChild(std::move(hThumb));
    chrome_ = chrome.get();
    appendChild(std::move(chrome));

    // ------------------------------------------------------------ 绑定滚轮与触控板双轴滚动
    bindWheel([this](const WheelEvent& wheel) {
        if (!layout_.horizontal.scrollable && !layout_.vertical.scrollable) {
            return false;
        }
        const double step = std::max(1.0, config_.wheelStep);
        double dx = -wheel.delta.x * step;
        double dy = -wheel.delta.y * step;

        // 当容器仅开启横向滚动且鼠标只发垂直滚轮时，自动将垂直滚轮映射为横向滚动
        if (!layout_.vertical.scrollable && layout_.horizontal.scrollable &&
            std::abs(dx) <= 1e-6 && std::abs(dy) > 1e-6) {
            dx = dy;
            dy = 0.0;
        }

        const core::Vec2d prev = scrollOffset_;
        setScrollOffset({scrollOffset_.x + dx, scrollOffset_.y + dy});
        return !(scrollOffset_ == prev);
    });

    // ------------------------------------------------------------ 绑定键盘导航滚动（目标/冒泡阶段）
    bindKey(KeyCode::Down, [this](const KeyEvent& ev) {
        if (!ev.down || !layout_.vertical.scrollable) {
            return false;
        }
        scrollBy({0.0, config_.wheelStep});
        return true;
    });
    bindKey(KeyCode::Up, [this](const KeyEvent& ev) {
        if (!ev.down || !layout_.vertical.scrollable) {
            return false;
        }
        scrollBy({0.0, -config_.wheelStep});
        return true;
    });
    bindKey(KeyCode::Right, [this](const KeyEvent& ev) {
        if (!ev.down || !layout_.horizontal.scrollable) {
            return false;
        }
        scrollBy({config_.wheelStep, 0.0});
        return true;
    });
    bindKey(KeyCode::Left, [this](const KeyEvent& ev) {
        if (!ev.down || !layout_.horizontal.scrollable) {
            return false;
        }
        scrollBy({-config_.wheelStep, 0.0});
        return true;
    });
    bindKey(KeyCode::PageDown, [this](const KeyEvent& ev) {
        if (!ev.down || !layout_.vertical.scrollable) {
            return false;
        }
        scrollBy({0.0, std::max(config_.wheelStep, layout_.viewport.height * 0.85)});
        return true;
    });
    bindKey(KeyCode::PageUp, [this](const KeyEvent& ev) {
        if (!ev.down || !layout_.vertical.scrollable) {
            return false;
        }
        scrollBy({0.0, -std::max(config_.wheelStep, layout_.viewport.height * 0.85)});
        return true;
    });
    bindKeyDown(KeyCode::Home, [this]() { scrollToStart(); }, KeyMod::Any);
    bindKeyDown(KeyCode::End, [this]() { scrollToEnd(); }, KeyMod::Any);
}

// ---------------------------------------------------------------- 内容管理

core::SceneNode* ScrollWidget::addContentChild(std::unique_ptr<core::SceneNode> child) {
    if (!child) {
        return nullptr;
    }
    core::SceneNode* raw = child.get();
    content_->appendChild(std::move(child));
    layoutDirty_ = true;
    return raw;
}

void ScrollWidget::clearContent() {
    content_->clearChildren();
    layoutDirty_ = true;
}

void ScrollWidget::setContentSize(core::Vec2d size) noexcept {
    autoContentSize_ = false;
    if (!(explicitContentSize_ == size)) {
        explicitContentSize_ = size;
        layoutDirty_ = true;
    }
}

void ScrollWidget::setAutoContentSize(bool enabled) noexcept {
    if (autoContentSize_ != enabled) {
        autoContentSize_ = enabled;
        layoutDirty_ = true;
    }
}

// ---------------------------------------------------------------- 配置与样式

void ScrollWidget::setConfig(const ScrollConfig& config) noexcept {
    config_ = config;
    syncDragOptions();
    layoutDirty_ = true;
}

void ScrollWidget::setOverflow(core::Overflow overflowX, core::Overflow overflowY) noexcept {
    if (config_.overflowX != overflowX || config_.overflowY != overflowY) {
        config_.overflowX = overflowX;
        config_.overflowY = overflowY;
        layoutDirty_ = true;
    }
}

void ScrollWidget::setOverflowX(core::Overflow mode) noexcept {
    if (config_.overflowX != mode) {
        config_.overflowX = mode;
        layoutDirty_ = true;
    }
}

void ScrollWidget::setOverflowY(core::Overflow mode) noexcept {
    if (config_.overflowY != mode) {
        config_.overflowY = mode;
        layoutDirty_ = true;
    }
}

void ScrollWidget::setBarStyle(const core::ScrollBarStyle& style) noexcept {
    barStyle_ = style;
    layoutDirty_ = true;
}

// ---------------------------------------------------------------- 滚动控制

void ScrollWidget::setScrollOffset(core::Vec2d offset) {
    normalizeOwnOrigin();
    if (layoutDirty_ || !chromeBuilt_ || !(frame().size() == lastContainerSize_)) {
        scrollOffset_ = offset;
        rebuildLayoutAndChrome();
        return;
    }

    const core::Vec2d clamped = layout_.clampScroll(offset);
    if (clamped == scrollOffset_) {
        return;
    }
    scrollOffset_ = clamped;
    syncFastPath();
    if (onScroll_) {
        onScroll_(scrollOffset_, layout_.progress);
    }
}

void ScrollWidget::setProgress(core::Vec2d normalizedProgress) {
    normalizeOwnOrigin();
    if (layoutDirty_ || !chromeBuilt_ || !(frame().size() == lastContainerSize_)) {
        rebuildLayoutAndChrome();
    }
    setScrollOffset(layout_.scrollForProgress(normalizedProgress));
}

// ---------------------------------------------------------------- 每帧更新

void ScrollWidget::onUpdate(const FrameContext& /*ctx*/) {
    normalizeOwnOrigin();

    const core::Vec2d containerSize = frame().size();
    const core::Vec2d measuredContent = resolveEffectiveContentSize();

    if (!chromeBuilt_ || layoutDirty_ || !(containerSize == lastContainerSize_) ||
        !(measuredContent == lastMeasuredContentSize_)) {
        rebuildLayoutAndChrome();
    }

    const core::ScrollBarVisualState state = currentVisualState();
    if (state.verticalHovered != lastVisualState_.verticalHovered ||
        state.verticalActive != lastVisualState_.verticalActive ||
        state.horizontalHovered != lastVisualState_.horizontalHovered ||
        state.horizontalActive != lastVisualState_.horizontalActive) {
        lastVisualState_ = state;
        core::updateScrollBars(*chrome_, layout_, frame().size(), barStyle_, lastVisualState_);
    }
}

// ---------------------------------------------------------------- 内部构建与快路径

void ScrollWidget::normalizeOwnOrigin() noexcept {
    const RectD box = frame();
    if (box.x != 0.0 || box.y != 0.0) {
        setTranslation(box.origin());
        setFrame({0.0, 0.0, box.width, box.height});
    }
}

void ScrollWidget::syncDragOptions() noexcept {
    DragOptions vpOpts = viewport_->options();
    vpOpts.axis = config_.dragContentToScroll ? DragAxis::Both : DragAxis::None;
    viewport_->setOptions(vpOpts);
}

core::Vec2d ScrollWidget::resolveEffectiveContentSize() const noexcept {
    if (!autoContentSize_) {
        return explicitContentSize_;
    }
    const core::Vec2d measured = core::measureContentBounds(*content_, config_.contentPadding);
    return {std::max(measured.x, explicitContentSize_.x),
            std::max(measured.y, explicitContentSize_.y)};
}

core::ScrollBarVisualState ScrollWidget::currentVisualState() const noexcept {
    core::ScrollBarVisualState state;
    const core::Vec2d ptr = pointerPosition();
    if (vThumb_ != nullptr && layout_.vertical.scrollable) {
        state.verticalHovered = vThumb_->hovered() || vThumb_->hitTest(ptr);
        state.verticalActive = vThumb_->active();
    }
    if (hThumb_ != nullptr && layout_.horizontal.scrollable) {
        state.horizontalHovered = hThumb_->hovered() || hThumb_->hitTest(ptr);
        state.horizontalActive = hThumb_->active();
    }
    return state;
}

void ScrollWidget::rebuildLayoutAndChrome() {
    const core::Vec2d containerSize = frame().size();
    const core::Vec2d effectiveContent = resolveEffectiveContentSize();

    core::ScrollLayoutInput input;
    input.containerSize = containerSize;
    input.contentSize = effectiveContent;
    input.scrollOffset = scrollOffset_;
    input.overflowX = config_.overflowX;
    input.overflowY = config_.overflowY;
    input.overlayScrollbars = config_.overlayScrollbars;
    input.metrics = barStyle_.metrics;

    const core::Vec2d prevScroll = scrollOffset_;
    layout_ = core::computeScrollLayout(input);
    scrollOffset_ = layout_.scroll;

    // 1. 同步底色节点
    const bool showBg = containerSize.x > 0.0 && containerSize.y > 0.0 &&
                        barStyle_.background.visible();
    bg_->setVisible(showBg);
    if (showBg) {
        bg_->setFrame({0.0, 0.0, containerSize.x, containerSize.y});
        core::SceneStyle s = bg_->style();
        s.fill = barStyle_.background;
        s.cornerRadius = std::max(0.0, barStyle_.containerRadius);
        bg_->setStyle(s);
    }

    // 2. 同步视口裁剪框、命中矩形与内容平移
    viewport_->setFrame(layout_.viewport);
    viewport_->setClipRect(layout_.viewport);
    syncDragOptions();
    content_->setTranslation({-scrollOffset_.x, -scrollOffset_.y});

    // 3. 构建滚动条装饰描述树并与 chrome_ 对账复用（原地保留 vThumb_/hThumb_ 等 Drag 实例！）
    lastVisualState_ = currentVisualState();
    if (auto desc = core::buildScrollBars(layout_, containerSize, barStyle_, lastVisualState_)) {
        (void)core::reconcileScene(*chrome_, *desc);
    }

    chromeBuilt_ = true;
    layoutDirty_ = false;
    lastContainerSize_ = containerSize;
    lastMeasuredContentSize_ = effectiveContent;

    if (!(scrollOffset_ == prevScroll) && onScroll_) {
        onScroll_(scrollOffset_, layout_.progress);
    }
}

void ScrollWidget::syncFastPath() {
    core::ScrollLayoutInput input;
    input.containerSize = lastContainerSize_;
    input.contentSize = lastMeasuredContentSize_;
    input.scrollOffset = scrollOffset_;
    input.overflowX = config_.overflowX;
    input.overflowY = config_.overflowY;
    input.overlayScrollbars = config_.overlayScrollbars;
    input.metrics = barStyle_.metrics;

    layout_ = core::computeScrollLayout(input);
    scrollOffset_ = layout_.scroll;
    lastVisualState_ = currentVisualState();

    // O(1) 快路径：仅修改 content_ 的平移分量并就地刷新滑块位置，零节点分配！
    content_->setTranslation({-scrollOffset_.x, -scrollOffset_.y});
    core::updateScrollBars(*chrome_, layout_, lastContainerSize_, barStyle_, lastVisualState_);
}

}  // namespace kiriyama::scene
