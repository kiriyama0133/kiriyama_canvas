#pragma once

// 交互组件基类：Widget = SceneNode + 一套"指针层"的公共账本。
//
// 为什么要有这一层：
//   悬停 / 按下 / 抬起 / 点击、指针进出、拖拽时的指针捕获，这些跟具体组件无关，
//   但每个组件都自己写一遍就是复制粘贴，而且很容易各写各的判定。
//   抽到这里之后，派生组件只需要关心"被按下之后我要做什么"。
//
// 指针状态由宿主每帧喂进来（FrameContext）。Widget 不 include 任何 SDL 头：
//   SDL 的 x/y/按下状态在 render 层被抹平成 PointerState 之后才进 scene，
//   所以这一层照样能在 node 里跑单元测试（见 tests/WidgetTest.cc）。
//
// 两个约定：
//   1. 命中区默认是自身的 frame（Group 的 localBounds 恒为空，不能直接拿 worldBounds
//      做判定）。frame 为空时退化成"包住整棵子树"。
//   2. tick() 只更新自己，tickTree() 按前序遍历更新子树里的所有 Widget ——
//      父先于子，父的位置先定下来，子的命中判定才准。

#include <string>

#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/SceneNode.h"

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

    /// 只更新自己。
    void tick(const FrameContext& ctx);
    /// 更新自己 + 子树里的所有 Widget（前序）。
    void tickTree(const FrameContext& ctx);

    // ------------------------------------------------------------ 指针状态

    /// 指针是否在本组件上。捕获期间恒为 true（拖出边界也不闪回未悬停态）。
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

    /// 指针捕获：移动到组件外面也继续收事件。按下时自动开始，松开自动结束。
    void capturePointer(bool capture) noexcept { captured_ = capture; }
    [[nodiscard]] bool hasPointerCapture() const noexcept { return captured_; }

    // ------------------------------------------------------------ 命中区

    /// 世界坐标下的命中矩形（AABB）。默认取 frame，frame 为空则取子树包围盒。
    /// 注意它只是"外接框查询"，不是命中判定——判定走 hitTest -> hitShape。
    [[nodiscard]] RectD hitBounds() const noexcept;
    [[nodiscard]] bool hitTest(const Vec2d& worldPoint) const noexcept;

protected:
    /// 命中判定钩子。默认：世界 AABB 包含点（frame / 子树包围盒）。
    /// 非矩形命中区（扇形极坐标、圆环、任意多边形）覆盖它做精确判定；
    /// updatePresence 只经由 hitTest 走到这里，不会绕过。
    [[nodiscard]] virtual bool hitShape(const Vec2d& worldPoint) const noexcept;

    /// 每帧回调。进来时指针状态（inside / pressed / clicked / delta）已经更新完毕。
    virtual void onUpdate(const FrameContext& ctx) { (void)ctx; }

    virtual void onPointerEnter() {}
    virtual void onPointerLeave() {}
    virtual void onPress() {}
    /// inside 是"松开时指针是否仍在组件内"，用来区分点击与拖出去松手。
    virtual void onRelease(bool inside) { (void)inside; }

    /// 本地坐标下的命中区。派生组件做边界约束时用它，别用 bounds()：
    /// Group 的 localBounds 恒为空，bounds() 只有子树范围，不含自身的 frame。
    [[nodiscard]] RectD localHitArea() const noexcept;

private:
    /// 重新判定"指针在不在我身上"，进出时触发 enter / leave。幂等，一帧可调多次。
    void updatePresence();

    Vec2d pointer_{};
    Vec2d pointerDelta_{};
    bool hasPointer_{false};
    bool down_{false};
    bool inside_{false};
    bool wasVisible_{false};
    bool pressed_{false};
    bool clicked_{false};
    bool captured_{false};
};

}  // namespace kiriyama::scene
