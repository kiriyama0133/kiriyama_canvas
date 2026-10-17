#include "kiriyama/render/SdlColor.h"

namespace kiriyama::render {
namespace {

[[nodiscard]] constexpr double clamp01(double value) noexcept {
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

}  // namespace

SDL_Color toSdlColor(const core::Color& color, double opacity) noexcept {
    const core::Rgba8 bytes = color.withOpacity(clamp01(opacity)).toBytes();
    return SDL_Color{bytes.r, bytes.g, bytes.b, bytes.a};
}

core::Color fromSdlColor(const SDL_Color& color) noexcept {
    return core::Color::FromBytes(color.r, color.g, color.b, color.a);
}

}  // namespace kiriyama::render
