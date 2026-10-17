#include "kiriyama/render/Font.h"

#include <algorithm>
#include <cmath>

#include "kiriyama/render/Utf8.h"

namespace kiriyama::render {
namespace {

constexpr double kMinPixelSize = 1.0;
constexpr double kMaxPixelSize = 512.0;
constexpr double kDebugCellSize = 8.0;  // SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE
constexpr double kDebugLineGapRatio = 0.25;

constexpr bool isPrintableAscii(char32_t codepoint) noexcept {
    return codepoint >= 32 && codepoint <= 126;
}

}  // namespace

Font::Font() noexcept : face_(builtinFace()), size_(12.0) {}

Font::Font(std::shared_ptr<const FontFace> face, double pixelSize)
    : face_(std::move(face)) {
    if (!face_) {
        face_ = builtinFace();
    }
    setSize(pixelSize);
}

std::shared_ptr<const FontFace> Font::builtinFace() {
    static const std::shared_ptr<const FontFace> face = std::make_shared<SdlDebugFontFace>();
    return face;
}

void Font::setSize(double pixelSize) noexcept {
    if (!std::isfinite(pixelSize)) {
        return;
    }
    size_ = std::clamp(pixelSize, kMinPixelSize, kMaxPixelSize);
}

double Font::measure(std::string_view utf8) const noexcept {
    double width = 0.0;
    const double spaceAdvance = advance(U' ');
    std::size_t offset = 0;
    while (offset < utf8.size()) {
        const DecodedCodepoint decoded = decodeUtf8(utf8, offset);
        offset += decoded.bytes == 0 ? 1 : decoded.bytes;

        const char32_t c = decoded.value;
        if (c == U'\n' || c == U'\r') {
            continue;  // 单行测量忽略换行，交给排版层处理
        }
        if (c == U'\t') {
            width += spaceAdvance * 4.0;
            continue;
        }
        width += advance(c);
    }
    return width;
}

Font Font::withSize(double pixelSize) const {
    return Font{face_, pixelSize};
}

// ---------------------------------------------------------------- 内置点阵字体

FontMetrics SdlDebugFontFace::metrics(double pixelSize) const noexcept {
    const double size = std::clamp(pixelSize, kMinPixelSize, kMaxPixelSize);
    FontMetrics out;
    out.pixelSize = size;
    out.ascent = size;  // 点阵字形直接贴着行的顶部画
    out.descent = 0.0;
    out.lineGap = size * kDebugLineGapRatio;
    (void)kDebugCellSize;
    return out;
}

double SdlDebugFontFace::advance(char32_t codepoint, double pixelSize) const noexcept {
    const double size = std::clamp(pixelSize, kMinPixelSize, kMaxPixelSize);
    // 控制字符不占位（空格由调用方决定要不要算宽度）
    if (codepoint < 32 || codepoint == 127) {
        return 0.0;
    }
    // 等宽方形：所有字符（含兜底方框）都是一个字号宽
    return size;
}

bool SdlDebugFontFace::supports(char32_t codepoint) const noexcept {
    return isPrintableAscii(codepoint);
}

}  // namespace kiriyama::render
