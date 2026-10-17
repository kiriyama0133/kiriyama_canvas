#pragma once

// core::Color ⇄ SDL_Color 的唯一转换点。
//
// 分离成独立头文件的原因：任何要画东西的 SDL 后端文件都需要它，
// 但没人愿意为了一个 4 行函数去 include 整个 TextRenderer.h。

#include <SDL3/SDL.h>

#include "kiriyama/core/Color.h"

namespace kiriyama::render {

/// core::Color（0~1）→ SDL_Color（0~255）。opacity 会额外乘进 alpha，
/// 用于父子 opacity 继承，不改变原色值。
[[nodiscard]] SDL_Color toSdlColor(const core::Color& color, double opacity = 1.0) noexcept;

/// SDL_Color → core::Color，读回表面像素、做色彩对比时用。
[[nodiscard]] core::Color fromSdlColor(const SDL_Color& color) noexcept;

}  // namespace kiriyama::render
