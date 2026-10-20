#pragma once

// 交互组件基类：Widget = SceneNode + 指针状态账本 + 三阶段路由事件（Tunnel / Target / Bubble）节点。
//
// 为什么要有这一层：
//   悬停 / 按下 / 抬起 / 点击、指针进出、拖拽时的指针捕获，以及父子组件嵌套时的
//   隧道捕获（Tunnel）与冒泡拦截（Bubble / stopPointerPropagation），统一由基类路由，
//   业务组件无需手写"子组件是否挡住父组件"的特判逻辑。

#include <functional>
#include <string>
#include <vector>

#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/SceneNode.h"
#include "kiriyama/scene/InputController.h"

namespace kiriyama::scene {

using core::RectD;
using core::SceneNode;
using core::Vec2d;

/// 指针状态。世界（逻辑）坐标。
struct PointerState {
    Vec2d position{};
    bool down{false};
};

/// 一帧的上下文：时间 + 指针。宿主每帧构造一次，喂给根 Widget。
struct FrameContext {
    double time{0.0};       ///< 秒
    double deltaTime{0.0};  ///< 秒
    PointerState pointer{};
};

class Widget : public SceneNode {
public:
    explicit Widget(std::string key = {});

    // ------------------------------------------------------------ 驱动

    /// 只更新自己（occludedByChild = true 表示指针已被更深层的子 Widget 拦截消费）。
    void tick(const FrameContext& ctx, bool occludedByChild = false);
    /// 先执行子树指针三阶段路由（Tunnel -> Target -> Bubble），再按前序遍历更新子树中的所有 Widget。
    void tickTree(const FrameContext& ctx);

    // ------------------------------------------------------------ 指针状态与冒泡控制

    /// 指针是否在本组件上（若已被上层子组件拦截消费则为 false）。捕获期间恒为 true。
    [[nodiscard]] bool pointerInside() const noexcept { return inside_ || captured_; }
    /// 悬停在上面且没按住。
    [[nodiscard]] bool hovered() const noexcept { return pointerInside() && !pressed_; }
    /// 在本组件上按下且仍按着。
    [[nodiscard]] bool pressed() const noexcept { return pressed_; }
    /// 本帧是否发生了一次完整点击（内部按下 + 内部抬起）。
    [[nodiscard]] bool clicked() const noexcept { return clicked_; }

    /// 指针位置 / 本帧位移（世界坐标）。
    [[nodiscard]] Vec2d pointerPosition() const noexcept { return pointer_; }
    [[nodiscard]] Vec2d pointerDelta() const noexcept { return pointerDelta_; }

    /// 指针捕获：移动到组件外面也继续收事件。按下时可开启，松开自动结束。
    void capturePointer(bool capture) noexcept { captured_ = capture; }
    [[nodiscard]] bool hasPointerCapture() const noexcept { return captured_; }

    /// 控制本组件命中时是否阻止指针事件向父组件冒泡（默认 false；滑块/按钮等独立交互控件设为 true）。
    Widget& setStopPointerPropagation(bool stop = true) noexcept {
        stopPointerPropagation_ = stop;
        return *this;
    }
    [[nodiscard]] bool stopPointerPropagation() const noexcept { return stopPointerPropagation_; }

    // ------------------------------------------------------------ 边框与阴影（容器外观）

    Widget& setCornerRadius(double radius) noexcept;
    [[nodiscard]] double cornerRadius() const noexcept { return style().cornerRadius; }

    Widget& setBorder(const core::Paint& color, double width = 1.0,
                      double cornerRadius = -1.0) noexcept;
    Widget& clearBorder() noexcept;

    Widget& setShadow(const core::BoxShadow& shadow) noexcept;
    Widget& setShadow(const core::Paint& color, const Vec2d& offset = {0.0, 4.0},
                      double blur = 8.0, double spread = 0.0) noexcept;
    Widget& clearShadow() noexcept;
    [[nodiscard]] const core::BoxShadow& shadow() const noexcept { return style().shadow; }

    // ------------------------------------------------------------ 输入控制器（Tunnel 捕获 / Bubble 冒泡）

    [[nodiscard]] InputController& input() noexcept { return input_; }
    [[nodiscard]] const InputController& input() const noexcept { return input_; }

    // 目标与冒泡阶段（Target / Bubble）绑定：
    Widget& bindKeyDown(KeyCode key, InputController::ActionFn action, KeyMod mods = KeyMod::None,
                        bool allowRepeat = true);
    Widget& bindKeyUp(KeyCode key, InputController::ActionFn action, KeyMod mods = KeyMod::None);
    Widget& bindKey(KeyCode key, InputController::KeyHandlerFn handler, KeyMod mods = KeyMod::Any);
    Widget& bindWheel(InputController::WheelHandlerFn handler);
    Widget& onClick(InputController::ActionFn action);

    // 隧道捕获阶段（Tunnel / Capture，父组件优先于子组件拦截）绑定：
    Widget& captureKeyDown(KeyCode key, InputController::ActionFn action,
                           KeyMod mods = KeyMod::None, bool allowRepeat = true);
    Widget& captureKeyUp(KeyCode key, InputController::ActionFn action,
                         KeyMod mods = KeyMod::None);
    Widget& captureKey(KeyCode key, InputController::KeyHandlerFn handler,
                       KeyMod mods = KeyMod::Any);
    Widget& captureWheel(InputController::WheelHandlerFn handler);

    /// 按当前 event.phase（Tunnel 或 Target/Bubble）分发事件到本组件。
    bool dispatchKey(const KeyEvent& event);
    bool dispatchWheel(const WheelEvent& event);
    bool dispatchPointer(const PointerEvent& event);

    // ------------------------------------------------------------ 命中区与光标查询

    /// 世界坐标下的命中矩形（AABB）。默认取 frame，frame 为空则取子树包围盒。
    [[nodiscard]] RectD hitBounds() const noexcept;
    [[nodiscard]] bool hitTest(const Vec2d& worldPoint) const noexcept;

    /// 递归查询 worldPoint 命中的最顶层光标形态（优先返回已捕获或拦截指针的子 Widget 光标）。
    [[nodiscard]] virtual core::CursorShape cursorAt(const Vec2d& worldPoint) const noexcept;

protected:
    /// 命中判定钩子。默认：世界 AABB 包含点（frame / 子树包围盒）。
    [[nodiscard]] virtual bool hitShape(const Vec2d& worldPoint) const noexcept;

    /// 查询子 Widget 是否已在 worldPoint 处接管光标（供重写 cursorAt 的复合容器调用）。
    [[nodiscard]] core::CursorShape childCursorAt(const Vec2d& worldPoint,
                                                  bool* consumedByChild = nullptr) const noexcept;

    /// 每帧回调。进来时指针状态（inside / pressed / clicked / delta）已经更新完毕。
    virtual void onUpdate(const FrameContext& ctx) { (void)ctx; }

    virtual void onPointerEnter() {}
    virtual void onPointerLeave() {}
    virtual void onPress() {}
    virtual void onRelease(bool inside) { (void)inside; }

    // 隧道阶段（Tunnel / Preview）钩子：
    virtual bool onPreviewKey(const KeyEvent& event) {
        (void)event;
        return false;
    }
    virtual bool onPreviewWheel(const WheelEvent& event) {
        (void)event;
        return false;
    }
    virtual bool onPreviewPointer(const PointerEvent& event) {
        (void)event;
        return false;
    }

    // 目标与冒泡阶段（Target / Bubble）钩子：
    virtual bool onKey(const KeyEvent& event) {
        (void)event;
        return false;
    }
    virtual bool onWheel(const WheelEvent& event) {
        (void)event;
        return false;
    }
    virtual bool onPointer(const PointerEvent& event) {
        (void)event;
        return false;
    }

    /// 本地坐标下的命中区。
    [[nodiscard]] RectD localHitArea() const noexcept;

private:
    void updatePresence(bool occludedByChild);

    Vec2d pointer_{};
    Vec2d pointerDelta_{};
    bool hasPointer_{false};
    bool down_{false};
    bool inside_{false};
    bool wasVisible_{false};
    bool pressed_{false};
    bool clicked_{false};
    bool captured_{false};
    bool stopPointerPropagation_{false};

    InputController input_{};
    std::vector<InputController::ActionFn> clickHandlers_{};
};

}  // namespace kiriyama::scene
