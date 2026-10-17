#pragma once

// TrueType 字体：把 SDL_ttf 包成 core 的 FontFace，并提供带缓存的加载器。
//
// 之前 `SdlDebugFontFace` 用的是 SDL3 自带的 8×8 点阵字体：只能 ASCII、非 8 倍数
// 字号靠整块缩放，中文全是方框。换成 TTF 之后：
//
//   - 任意字号都是真正的矢量光栅化（带抗锯齿），不再有 8px 网格感；
//   - 中文 / 图标只要字体里有字形就能画；
//   - 度量（ascent / descent / lineSkip / advance）来自字体本身，
//     TextLayout 的断行、对齐、省略号全部自动变准。
//
// 两个类：
//   TtfFontFace  —— FontFace 实现，按字号惰性打开 TTF_Font 并缓存
//   FontLibrary  —— 路径解析 + 共享缓存，避免同一支字体被重复打开
//
// 生命周期：TTF_Init / TTF_Quit 由 render::App 负责；FontLibrary 只缓存句柄，
// 不做全局初始化。在 App 之外使用时会自动补一次 TTF_Init。

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <SDL3_ttf/SDL_ttf.h>

#include "Font.h"

namespace kiriyama::render {

/// 一支 TTF/OTF 字体。同一支字体在不同字号下共用这一个对象，
/// 内部为每个字号维护一个 TTF_Font（SDL_ttf 的字体句柄是带字号的）。
class TtfFontFace final : public FontFace {
public:
    /// 打开字体文件。失败返回 nullptr（并写 SDL 日志）。
    [[nodiscard]] static std::shared_ptr<const TtfFontFace> open(std::string_view path);

    ~TtfFontFace() override;

    TtfFontFace(const TtfFontFace&) = delete;
    TtfFontFace& operator=(const TtfFontFace&) = delete;

    [[nodiscard]] std::string_view family() const noexcept override { return family_; }
    [[nodiscard]] FontMetrics metrics(double pixelSize) const noexcept override;
    [[nodiscard]] double advance(char32_t codepoint, double pixelSize) const noexcept override;
    [[nodiscard]] bool supports(char32_t codepoint) const noexcept override;

    /// 某字号对应的 TTF_Font，光栅化字形时用。失败返回 nullptr。
    [[nodiscard]] TTF_Font* handleFor(double pixelSize) const noexcept;

    /// 字号量化后的 key（1/4 px）。渲染层的字形缓存用它，避免浮点比较。
    [[nodiscard]] static int sizeKey(double pixelSize) noexcept;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] bool valid() const noexcept { return !family_.empty(); }

private:
    TtfFontFace(std::string path, std::string family);

    mutable std::unordered_map<int, TTF_Font*> fonts_;
    std::string path_;
    std::string family_;
};

/// 字体加载器：按路径缓存 FontFace，负责把 "public/xxx.ttf" 这类相对名
/// 解析到当前环境下真正可读的位置（wasm 里是 preload 的 /public/xxx.ttf）。
class FontLibrary {
public:
    [[nodiscard]] static FontLibrary& shared();

    /// 加载（或复用）字体。失败返回 nullptr。
    [[nodiscard]] std::shared_ptr<const FontFace> load(std::string_view pathOrName);

    /// 便捷入口：加载 + 指定字号。字体打不开时回退到内置点阵字体。
    [[nodiscard]] Font font(std::string_view pathOrName, double pixelSize);

    /// 丢掉缓存。已经发出去的 Font 仍然有效（引用计数）。
    void clear();

    /// 把相对文件名解析成可读路径；找不到时原样返回。
    [[nodiscard]] static std::string resolve(std::string_view pathOrName);

    /// 当前缓存里的字体数量（调试用）。
    [[nodiscard]] std::size_t loadedCount() const noexcept { return cache_.size(); }

private:
    FontLibrary() = default;

    std::unordered_map<std::string, std::weak_ptr<const TtfFontFace>> cache_;
};

}  // namespace kiriyama::render
