#pragma once

// 入口胶水：把 SDL3 的四个 main-callback 函数接给 render::App。
//
// 用法（一个可执行文件里只能用一次）：
//
//   #include "kiriyama/render/AppMain.h"
//
//   class MyHost final : public kiriyama::render::AppDelegate { ... };
//
//   KIRIYAMA_MAIN(MyHost)
//
// 为什么单独一个头：SDL_MAIN_USE_CALLBACKS 必须在 <SDL3/SDL_main.h> 之前
// 定义，放在这里就能保证顺序，业务代码不用关心。

#ifndef SDL_MAIN_USE_CALLBACKS
#define SDL_MAIN_USE_CALLBACKS 1
#endif

#include <memory>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "App.h"

/// 定义 SDL_AppInit / SDL_AppIterate / SDL_AppEvent / SDL_AppQuit。
///
/// 宏里有函数定义，所以只能在 .cc 里用，且整个可执行文件唯一一次。
#define KIRIYAMA_MAIN(DelegateType)                                              \
    SDL_AppResult SDL_AppInit(void** appstate, int, char**) {                     \
        auto app = std::make_unique<::kiriyama::render::App>(                     \
            std::make_unique<DelegateType>());                                   \
        if (!app->start()) {                                                     \
            return SDL_APP_FAILURE;                                              \
        }                                                                        \
        *appstate = app.release();                                               \
        return SDL_APP_CONTINUE;                                                 \
    }                                                                            \
    SDL_AppResult SDL_AppIterate(void* appstate) {                                \
        auto* app = static_cast<::kiriyama::render::App*>(appstate);              \
        return (app != nullptr && app->step()) ? SDL_APP_CONTINUE                 \
                                               : SDL_APP_FAILURE;                 \
    }                                                                            \
    SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {                \
        auto* app = static_cast<::kiriyama::render::App*>(appstate);              \
        if (app == nullptr || event == nullptr) {                                 \
            return SDL_APP_FAILURE;                                               \
        }                                                                        \
        return app->dispatch(*event) ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;        \
    }                                                                            \
    void SDL_AppQuit(void* appstate, SDL_AppResult) {                             \
        delete static_cast<::kiriyama::render::App*>(appstate);                   \
    }
