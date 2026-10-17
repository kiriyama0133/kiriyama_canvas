#pragma once

// SDL → scene 的胶水：把 App 的时钟与指针抹平成 scene::FrameContext。
//
// 有这一层，scene/ 里一行 SDL 都不需要 include，组件照样能在 node 里跑单元测试。
// 宿主每帧构造一次，喂给根 Widget：
//
//   root->tickTree(render::frameContext(app));

#include "kiriyama/render/App.h"
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

}  // namespace kiriyama::render
