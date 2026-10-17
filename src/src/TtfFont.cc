#include "kiriyama/render/TtfFont.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <SDL3/SDL.h>

namespace kiriyama::render {
namespace {

constexpr double kProbeSize = 16.0;

[[nodiscard]] std::string fileNameOf(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return std::string{slash == std::string_view::npos ? path : path.substr(slash + 1)};
}

[[nodiscard]] std::string stemOf(std::string_view path) {
    std::string name = fileNameOf(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        name.resize(dot);
    }
    return name;
}

[[nodiscard]] bool readable(const std::string& path) {
    SDL_PathInfo info{};
    return !path.empty() && SDL_GetPathInfo(path.c_str(), &info);
}

}  // namespace

// ---------------------------------------------------------------- TtfFontFace

TtfFontFace::TtfFontFace(std::string path, std::string family)
    : path_(std::move(path)), family_(std::move(family)) {}

TtfFontFace::~TtfFontFace() {
    // TTF_Quit 之后句柄已经失效，再 Close 会踩到已释放的全局状态。
    // 正常生命周期由 App 保证（先析构渲染器/字体，最后 TTF_Quit），
    // 这里只是兜住"字体比 TTF 活得久"的静态析构场景。
    const bool live = TTF_WasInit() > 0;
    for (auto& entry : fonts_) {
        if (entry.second != nullptr && live) {
            TTF_CloseFont(entry.second);
        }
    }
    fonts_.clear();
}

std::shared_ptr<const TtfFontFace> TtfFontFace::open(std::string_view path) {
    // 正常路径下 render::App 已经 TTF_Init 过了；这里兜底一次，
    // 让脱离 App 的用法（测试、工具）也能工作。
    if (TTF_WasInit() == 0 && !TTF_Init()) {
        SDL_Log("kiriyama: TTF_Init 失败: %s", SDL_GetError());
        return nullptr;
    }

    const std::string resolved = FontLibrary::resolve(path);
    if (!readable(resolved)) {
        SDL_Log("kiriyama: 字体文件不存在: %s", std::string(path).c_str());
        return nullptr;
    }

    TTF_Font* probe = TTF_OpenFont(resolved.c_str(), static_cast<float>(kProbeSize));
    if (probe == nullptr) {
        SDL_Log("kiriyama: 打开字体失败 %s: %s", resolved.c_str(), SDL_GetError());
        return nullptr;
    }

    std::string family = stemOf(resolved);
    if (const char* name = TTF_GetFontFamilyName(probe); name != nullptr && *name != '\0') {
        family = name;
    }
    TTF_CloseFont(probe);

    return std::shared_ptr<const TtfFontFace>(new TtfFontFace(resolved, std::move(family)));
}

int TtfFontFace::sizeKey(double pixelSize) noexcept {
    const double clamped = std::clamp(pixelSize, 0.25, 1024.0);
    return static_cast<int>(std::lround(clamped * 4.0));
}

TTF_Font* TtfFontFace::handleFor(double pixelSize) const noexcept {
    const int key = sizeKey(pixelSize);
    if (const auto it = fonts_.find(key); it != fonts_.end()) {
        return it->second;
    }

    // 只按 1/4 px 粒度开字体：图表里的字号是 handful 个档位，
    // 每个档位一个 TTF_Font 完全付得起。
    const float size = static_cast<float>(key) / 4.0f;
    TTF_Font* font = TTF_OpenFont(path_.c_str(), size);
    if (font != nullptr) {
        TTF_SetFontKerning(font, 1);
        TTF_SetFontHinting(font, TTF_HINTING_NORMAL);
    } else {
        SDL_Log("kiriyama: 以 %.2fpx 打开字体失败 %s: %s", size, path_.c_str(), SDL_GetError());
    }
    fonts_.emplace(key, font);
    return font;
}

FontMetrics TtfFontFace::metrics(double pixelSize) const noexcept {
    FontMetrics result;
    result.pixelSize = pixelSize;

    TTF_Font* font = handleFor(pixelSize);
    if (font == nullptr) {
        result.ascent = pixelSize;
        return result;
    }

    const int ascent = TTF_GetFontAscent(font);
    const int descent = TTF_GetFontDescent(font);  // SDL_ttf 返回负数
    const int lineSkip = TTF_GetFontLineSkip(font);

    result.ascent = ascent > 0 ? static_cast<double>(ascent) : pixelSize * 0.8;
    result.descent = descent < 0 ? static_cast<double>(-descent) : 0.0;

    const double natural = result.ascent + result.descent;
    result.lineGap = lineSkip > natural ? static_cast<double>(lineSkip) - natural : 0.0;
    return result;
}

double TtfFontFace::advance(char32_t codepoint, double pixelSize) const noexcept {
    TTF_Font* font = handleFor(pixelSize);
    if (font == nullptr) {
        return 0.0;
    }

    int minx = 0;
    int maxx = 0;
    int miny = 0;
    int maxy = 0;
    int advance = 0;
    if (!TTF_GetGlyphMetrics(font, static_cast<Uint32>(codepoint), &minx, &maxx, &miny, &maxy,
                             &advance)) {
        return 0.0;
    }
    return static_cast<double>(advance);
}

bool TtfFontFace::supports(char32_t codepoint) const noexcept {
    TTF_Font* font = handleFor(12.0);
    return font != nullptr && TTF_FontHasGlyph(font, static_cast<Uint32>(codepoint));
}

// ---------------------------------------------------------------- FontLibrary

FontLibrary& FontLibrary::shared() {
    static FontLibrary library;
    return library;
}

std::string FontLibrary::resolve(std::string_view pathOrName) {
    const std::string raw{pathOrName};
    if (raw.empty()) {
        return raw;
    }
    if (readable(raw)) {
        return raw;
    }

    // wasm 构建把 public/ 目录 preload 到 /public/；原生构建则是相对路径。
    const std::string base = fileNameOf(raw);
    const std::string candidates[] = {
        "/" + raw,
        "/public/" + base,
        "public/" + base,
        "/" + base,
        base,
    };
    for (const std::string& candidate : candidates) {
        if (readable(candidate)) {
            return candidate;
        }
    }
    return raw;
}

std::shared_ptr<const FontFace> FontLibrary::load(std::string_view pathOrName) {
    const std::string resolved = resolve(pathOrName);

    if (const auto it = cache_.find(resolved); it != cache_.end()) {
        if (auto existing = it->second.lock()) {
            return existing;
        }
    }

    std::shared_ptr<const TtfFontFace> face = TtfFontFace::open(resolved);
    if (!face) {
        cache_.erase(resolved);
        return nullptr;
    }
    cache_[resolved] = face;
    return face;
}

Font FontLibrary::font(std::string_view pathOrName, double pixelSize) {
    if (std::shared_ptr<const FontFace> face = load(pathOrName)) {
        return Font(std::move(face), pixelSize);
    }
    SDL_Log("kiriyama: 字体不可用，回退到内置点阵字体: %s", std::string(pathOrName).c_str());
    return Font(Font::builtinFace(), pixelSize);
}

void FontLibrary::clear() { cache_.clear(); }

}  // namespace kiriyama::render
