#pragma once

// SDL → scene 的胶水：
//   1. frameContext(app)：把 App 的时钟与指针抹平成 scene::FrameContext；
//   2. dispatchInputEvent(app, event, root, globalController)：把 SDL_Event（键盘 / 滚轮）
//      翻译成平台无关的 scene::KeyEvent / WheelEvent，路由给场景树中的 Widget 或全局 InputController。
//
// 有这一层，scene/ 里一行 SDL 都不需要 include，组件照样能在 node 里跑单元测试。

#include "kiriyama/render/App.h"
#include "kiriyama/scene/InputController.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::render {

[[nodiscard]] inline scene::FrameContext frameContext(const App& app) noexcept {
    scene::FrameContext ctx;
    ctx.time = app.time();
    ctx.deltaTime = app.deltaTime();
    ctx.pointer.position = app.pointer();
    ctx.pointer.down = app.pointerDown();
    return ctx;
}

[[nodiscard]] inline scene::KeyMod toKeyMod(SDL_Keymod sdlMod) noexcept {
    using scene::KeyMod;
    KeyMod mods = KeyMod::None;
    if ((sdlMod & SDL_KMOD_SHIFT) != 0) {
        mods = mods | KeyMod::Shift;
    }
    if ((sdlMod & SDL_KMOD_CTRL) != 0) {
        mods = mods | KeyMod::Ctrl;
    }
    if ((sdlMod & SDL_KMOD_ALT) != 0) {
        mods = mods | KeyMod::Alt;
    }
    if ((sdlMod & SDL_KMOD_GUI) != 0) {
        mods = mods | KeyMod::Super;
    }
    return mods;
}

[[nodiscard]] inline scene::KeyCode toKeyCode(SDL_Keycode key) noexcept {
    using scene::KeyCode;
    switch (key) {
        case SDLK_UP:        return KeyCode::Up;
        case SDLK_DOWN:      return KeyCode::Down;
        case SDLK_LEFT:      return KeyCode::Left;
        case SDLK_RIGHT:     return KeyCode::Right;
        case SDLK_PAGEDOWN:  return KeyCode::PageDown;
        case SDLK_PAGEUP:    return KeyCode::PageUp;
        case SDLK_HOME:      return KeyCode::Home;
        case SDLK_END:       return KeyCode::End;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  return KeyCode::Enter;
        case SDLK_ESCAPE:    return KeyCode::Escape;
        case SDLK_SPACE:     return KeyCode::Space;
        case SDLK_TAB:       return KeyCode::Tab;
        case SDLK_BACKSPACE: return KeyCode::Backspace;
        case SDLK_DELETE:    return KeyCode::Delete;
        default:
            break;
    }
    if (key >= SDLK_A && key <= SDLK_Z) {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::A) + (key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::Num0) + (key - SDLK_0));
    }
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(KeyCode::F1) + (key - SDLK_F1));
    }
    return KeyCode::Unknown;
}

/// 将 SDL 键盘 / 滚轮事件翻译并分发给场景树（优先命中指针下的 Widget，未消费时回落到 globalController）。
inline bool dispatchInputEvent(const App& app, const SDL_Event& event, core::SceneNode& root,
                               const scene::InputController* globalController = nullptr) {
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        scene::WheelEvent wheel;
        wheel.delta = core::Vec2d{static_cast<double>(event.wheel.x),
                                  static_cast<double>(event.wheel.y)};
        wheel.pointer = app.pointer();
        if (scene::dispatchWheelToTree(root, wheel)) {
            return true;
        }
        if (globalController != nullptr && globalController->handleWheel(wheel)) {
            return true;
        }
        return false;
    }

    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        scene::KeyEvent keyEvent;
        keyEvent.key = toKeyCode(event.key.key);
        keyEvent.mods = toKeyMod(event.key.mod);
        keyEvent.down = (event.type == SDL_EVENT_KEY_DOWN);
        keyEvent.repeat = event.key.repeat;
        if (scene::dispatchKeyToTree(root, keyEvent, app.pointer())) {
            return true;
        }
        if (globalController != nullptr && globalController->handleKey(keyEvent)) {
            return true;
        }
        return false;
    }

    return false;
}

}  // namespace kiriyama::render
