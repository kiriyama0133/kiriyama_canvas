#pragma once

// 平台无关的输入事件与三阶段路由控制器（scene 层，零 SDL 依赖）：
//   · 把键盘按键、修饰键组合（Ctrl/Shift/Alt/Super）、滚轮与指针事件抽象为纯逻辑路由事件（RoutedEvent）；
//   · 完整支持 W3C / WPF 标准三阶段事件传播：
//       1) Tunnel（隧道捕获阶段：从根 Widget 沿命中路径向下传递至 Target 之前）
//       2) Target（目标阶段：在最深层命中的 Target Widget 本身上触发）
//       3) Bubble（冒泡阶段：从 Target 的父节点沿命中路径向上回传至根 Widget）
//   · 任一阶段调用 event.consume() / event.stopPropagation() 或处理器返回 true 即终止后续传播。

#include <cstdint>
#include <functional>
#include <vector>

#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/SceneNode.h"

namespace kiriyama::scene {

class Widget;

/// 路由事件所处的传播阶段。
enum class EventPhase : std::uint8_t {
    Tunnel,  ///< 隧道捕获阶段（Capture：从根向叶）
    Target,  ///< 目标阶段（最深层命中节点自身）
    Bubble,  ///< 冒泡阶段（Bubble：从叶向根）
};

/// 路由事件公共基类：携带传播阶段、目标节点与消费拦截状态。
struct RoutedEvent {
    EventPhase phase{EventPhase::Target};
    Widget* target{nullptr};         ///< 最深层命中的目标 Widget
    Widget* currentTarget{nullptr};  ///< 当前正在处理该事件的 Widget
    mutable bool handled{false};     ///< 是否已被消费（消费后停止向后续阶段/节点传播）

    void consume() const noexcept { handled = true; }
    void stopPropagation() const noexcept { handled = true; }
};

/// 平台无关按键码（由 render 层从 SDL_Keycode 翻译进来）。
enum class KeyCode : std::uint16_t {
    Unknown = 0,

    // 导航与编辑
    Up,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    Home,
    End,
    Enter,
    Escape,
    Space,
    Tab,
    Backspace,
    Delete,

    // 字母 A-Z
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // 数字 0-9
    Num0, Num1, Num2, Num3, Num4,
    Num5, Num6, Num7, Num8, Num9,

    // 功能键 F1-F12
    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12,
};

/// 修饰键掩码。Any 表示匹配时不限制修饰键状态。
enum class KeyMod : std::uint8_t {
    None  = 0,
    Shift = 1 << 0,
    Ctrl  = 1 << 1,
    Alt   = 1 << 2,
    Super = 1 << 3,
    Any   = 0xFF,
};

[[nodiscard]] constexpr KeyMod operator|(KeyMod lhs, KeyMod rhs) noexcept {
    return static_cast<KeyMod>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr KeyMod operator&(KeyMod lhs, KeyMod rhs) noexcept {
    return static_cast<KeyMod>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool hasMod(KeyMod mods, KeyMod required) noexcept {
    return (mods & required) == required;
}

/// 键盘路由事件。
struct KeyEvent : RoutedEvent {
    KeyCode key{KeyCode::Unknown};
    KeyMod mods{KeyMod::None};
    bool down{true};     ///< true = 按下，false = 抬起
    bool repeat{false};  ///< 长按自动连发

    constexpr KeyEvent() noexcept = default;
    constexpr KeyEvent(KeyCode k, KeyMod m = KeyMod::None, bool d = true, bool r = false) noexcept
        : key(k), mods(m), down(d), repeat(r) {}
};

/// 鼠标滚轮 / 触控板滚动路由事件（世界/逻辑坐标）。
struct WheelEvent : RoutedEvent {
    core::Vec2d delta{};    ///< 滚动增量（y > 0 向上滚，y < 0 向下滚，同 SDL 约定）
    core::Vec2d pointer{};  ///< 滚动发生时的指针逻辑坐标

    constexpr WheelEvent() noexcept = default;
    constexpr WheelEvent(core::Vec2d d, core::Vec2d p = {}) noexcept : delta(d), pointer(p) {}
};

/// 指针路由事件类型。
enum class PointerEventType : std::uint8_t {
    Move,
    Down,
    Up,
};

/// 指针路由事件（世界/逻辑坐标）。
struct PointerEvent : RoutedEvent {
    PointerEventType type{PointerEventType::Move};
    core::Vec2d position{};
    core::Vec2d delta{};
    bool down{false};
};

/// 输入控制器：管理按键与滚轮在 Tunnel（捕获）或 Bubble（冒泡）阶段的监听器。
class InputController {
public:
    using BindingId = std::uint64_t;
    using ActionFn = std::function<void()>;
    using KeyHandlerFn = std::function<bool(const KeyEvent&)>;
    using WheelHandlerFn = std::function<bool(const WheelEvent&)>;

    InputController() = default;

    // ------------------------------------------------------------ 按键绑定（capture = true 时在隧道阶段触发）

    /// 绑定指定按键按下时触发的动作。mods = KeyMod::Any 时忽略修饰键状态。
    BindingId bindKeyDown(KeyCode key, ActionFn action, KeyMod mods = KeyMod::None,
                          bool allowRepeat = true, bool capture = false);

    /// 绑定指定按键抬起时触发的动作。
    BindingId bindKeyUp(KeyCode key, ActionFn action, KeyMod mods = KeyMod::None,
                        bool capture = false);

    /// 绑定通用按键处理器（key == KeyCode::Unknown 表示监听任意键）。返回 true 表示消费该事件。
    BindingId bindKey(KeyCode key, KeyHandlerFn handler, KeyMod mods = KeyMod::Any,
                      bool capture = false);

    // ------------------------------------------------------------ 滚轮绑定（capture = true 时在隧道阶段触发）

    /// 绑定滚轮处理器。返回 true 表示消费该事件。
    BindingId bindWheel(WheelHandlerFn handler, bool capture = false);

    // ------------------------------------------------------------ 管理与分发

    bool unbind(BindingId id);
    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept {
        return keyBindings_.empty() && wheelBindings_.empty();
    }

    /// 触发本控制器上匹配的按键绑定；capturePhase 区分隧道阶段（true）与目标/冒泡阶段（false）。
    bool handleKey(const KeyEvent& event, bool capturePhase = false) const;

    /// 触发本控制器上匹配的滚轮绑定；capturePhase 区分隧道阶段（true）与目标/冒泡阶段（false）。
    bool handleWheel(const WheelEvent& event, bool capturePhase = false) const;

private:
    struct KeyBinding {
        BindingId id{0};
        KeyCode key{KeyCode::Unknown};
        KeyMod mods{KeyMod::Any};
        bool onlyDown{false};
        bool onlyUp{false};
        bool allowRepeat{true};
        bool capture{false};
        KeyHandlerFn handler{};
    };

    struct WheelBinding {
        BindingId id{0};
        bool capture{false};
        WheelHandlerFn handler{};
    };

    BindingId nextId_{1};
    std::vector<KeyBinding> keyBindings_{};
    std::vector<WheelBinding> wheelBindings_{};
};

/// 查找场景树中位于 pointer 下的最顶层可见 Widget（若存在持有 pointerCapture 的 Widget 则优先返回它）。
[[nodiscard]] Widget* findTopmostWidgetAt(core::SceneNode& root, const core::Vec2d& pointer);

/// 构建从 root 到 target 的 Widget 路由路径 [rootWidget, ..., target]（用于 Tunnel -> Target -> Bubble）。
[[nodiscard]] std::vector<Widget*> buildWidgetEventPath(Widget* target);

/// 向场景树按 Tunnel -> Target -> Bubble 三阶段路由分发按键事件。
bool dispatchKeyToTree(core::SceneNode& root, KeyEvent event, const core::Vec2d& pointer);

/// 向场景树按 Tunnel -> Target -> Bubble 三阶段路由分发滚轮事件。
bool dispatchWheelToTree(core::SceneNode& root, WheelEvent event);

}  // namespace kiriyama::scene
