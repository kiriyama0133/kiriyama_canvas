#pragma once

// 通用可滚动容器组件（scene 层，零 SDL 依赖）：
//   · 支持仿 CSS 的 overflowX / overflowY（Hidden / Auto / Scroll）双向独立控制；
//   · 拖拽职责彻底分离：滑块拖拽（vThumb_ / hThumb_）与内容拖拽（viewport_）全部交由 scene::Drag 组件驱动，
//     ScrollWidget 自身不维护任何指针按下/抬起/捕获状态机；
//   · 内部拆分为三个正交子树分支：
//       1) bg_       （容器底色）
//       2) viewport_ （带 clipRect 的 Drag 视口组） -> content_（承载用户子组件，仅通过平移 -scrollOffset 滚动，零重建）
//       3) chrome_   （由 core::buildScrollBars 生成并通过 reconcileScene 原地对账复用 Drag/Widget 节点的滚动条装饰组）
//   · 支持自动测量 content_ 内子组件的逻辑宽高包络（measureContentBounds），也支持手动指定 setContentSize()；
//   · 同时维护像素偏移 scrollOffset 与归一化进度 progress ∈ [0, 1]² 的双向读写。

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "kiriyama/core/SceneBuilder.h"
#include "kiriyama/core/ScrollLayout.h"
#include "kiriyama/scene/Drag.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

struct ScrollConfig {
    core::Overflow overflowX{core::Overflow::Auto};
    core::Overflow overflowY{core::Overflow::Auto};
    bool overlayScrollbars{true};     ///< true: 滚动条悬浮于内容上方；false: 预留通道挤压视口
    bool dragContentToScroll{false};  ///< true: 允许在内容区按住拖拽滚屏（由内部 viewport_ Drag 组件驱动）
    double wheelStep{48.0};           ///< 滚轮每格对应的像素步长
    core::Vec2d contentPadding{};     ///< 自动测量子组件逻辑边界时附加的右下内边距
};

class ScrollWidget : public Widget {
public:
    using ScrollCallback = std::function<void(core::Vec2d scrollOffset, core::Vec2d progress)>;

    explicit ScrollWidget(std::string key = "scroll");

    // ------------------------------------------------------------ 子组件内容管理

    /// 返回承载用户子组件的内容容器节点（挂在 viewport_ 裁剪组内）。
    [[nodiscard]] core::SceneNode& content() noexcept { return *content_; }
    [[nodiscard]] const core::SceneNode& content() const noexcept { return *content_; }

    /// 向滚动内容区挂载一个子节点（Widget 或普通 SceneNode），返回其裸指针。
    core::SceneNode* addContentChild(std::unique_ptr<core::SceneNode> child);

    /// 在滚动内容区原地构造并挂载子组件。
    template <typename T, typename... Args>
    T* emplaceContentChild(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = node.get();
        addContentChild(std::move(node));
        return raw;
    }

    /// 清空滚动内容区中的所有子节点。
    void clearContent();

    /// 显式指定内容逻辑宽高（关闭自动测量）。
    void setContentSize(core::Vec2d size) noexcept;

    /// 开启或关闭子组件逻辑边界自动测量（默认开启）。
    void setAutoContentSize(bool enabled = true) noexcept;
    [[nodiscard]] bool autoContentSize() const noexcept { return autoContentSize_; }

    /// 当前生效的内容逻辑尺寸。
    [[nodiscard]] core::Vec2d contentSize() const noexcept { return layout_.contentSize; }

    /// 当外部直接修改了子组件尺寸或位置时，通知容器重新测量布局。
    void markContentDirty() noexcept { layoutDirty_ = true; }

    // ------------------------------------------------------------ 配置与样式定制

    void setConfig(const ScrollConfig& config) noexcept;
    [[nodiscard]] const ScrollConfig& config() const noexcept { return config_; }

    void setOverflow(core::Overflow overflowX, core::Overflow overflowY) noexcept;
    void setOverflowX(core::Overflow mode) noexcept;
    void setOverflowY(core::Overflow mode) noexcept;

    void setBarStyle(const core::ScrollBarStyle& style) noexcept;
    [[nodiscard]] const core::ScrollBarStyle& barStyle() const noexcept { return barStyle_; }

    // ------------------------------------------------------------ 像素偏移与归一化进度

    [[nodiscard]] core::Vec2d scrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] double scrollX() const noexcept { return scrollOffset_.x; }
    [[nodiscard]] double scrollY() const noexcept { return scrollOffset_.y; }

    void setScrollOffset(core::Vec2d offset);
    void setScrollX(double x) { setScrollOffset({x, scrollOffset_.y}); }
    void setScrollY(double y) { setScrollOffset({scrollOffset_.x, y}); }
    void scrollBy(core::Vec2d delta) { setScrollOffset(scrollOffset_ + delta); }

    void scrollToStart() { setScrollOffset({0.0, 0.0}); }
    void scrollToEnd() { setScrollOffset(layout_.maxScroll); }

    /// 归一化滚动进度 progress ∈ [0, 1]²（不可滚动轴恒为 0）。
    [[nodiscard]] core::Vec2d progress() const noexcept { return layout_.progress; }
    [[nodiscard]] double progressX() const noexcept { return layout_.progress.x; }
    [[nodiscard]] double progressY() const noexcept { return layout_.progress.y; }

    /// 按归一化进度 [0, 1] 设置滚动位置。
    void setProgress(core::Vec2d normalizedProgress);
    void setProgressX(double px) { setProgress({px, layout_.progress.y}); }
    void setProgressY(double py) { setProgress({layout_.progress.x, py}); }

    [[nodiscard]] core::Vec2d maxScroll() const noexcept { return layout_.maxScroll; }
    [[nodiscard]] const core::ScrollLayout& layout() const noexcept { return layout_; }

    void onScroll(ScrollCallback callback) { onScroll_ = std::move(callback); }

protected:
    void onUpdate(const FrameContext& ctx) override;

private:
    void normalizeOwnOrigin() noexcept;
    void syncDragOptions() noexcept;
    [[nodiscard]] core::Vec2d resolveEffectiveContentSize() const noexcept;
    [[nodiscard]] core::ScrollBarVisualState currentVisualState() const noexcept;
    void rebuildLayoutAndChrome();
    void syncFastPath();

    ScrollConfig config_{};
    core::ScrollBarStyle barStyle_{};
    core::ScrollBarVisualState lastVisualState_{};
    core::ScrollLayout layout_{};

    bool autoContentSize_{true};
    core::Vec2d explicitContentSize_{};
    core::Vec2d lastMeasuredContentSize_{-1.0, -1.0};
    core::Vec2d lastContainerSize_{-1.0, -1.0};
    core::Vec2d scrollOffset_{};
    core::Vec2d dragStartScroll_{};

    bool chromeBuilt_{false};
    bool layoutDirty_{true};

    core::SceneNode* bg_{nullptr};
    Drag* viewport_{nullptr};
    core::SceneNode* content_{nullptr};
    core::SceneNode* chrome_{nullptr};

    Widget* vTrack_{nullptr};
    Drag* vThumb_{nullptr};
    Widget* hTrack_{nullptr};
    Drag* hThumb_{nullptr};

    ScrollCallback onScroll_{};
};

}  // namespace kiriyama::scene
