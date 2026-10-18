#pragma once

// 字体：字体面（FontFace）+ 字号（Font）。
//
// 这一层刻意不依赖 SDL：字体面只回答"某码点在某个字号下有多宽、上下伸展多少"，
// 具体怎么把字形画到屏幕上是渲染层的事。好处是文本排版可以在没有窗口的环境
// （node 里跑的单元测试）完整验证。
//
// 当前有两个实现：
//   TtfFontFace（TtfFont.h，推荐）—— SDL_ttf 光栅化，任意字号、中文、图标；
//   SdlDebugFontFace（本文件）    —— SDL3 自带的 8×8 点阵，只能 ASCII，
//                                    字体加载失败时作为兜底，也用于无字体资源的测试。
// 两者都实现同一接口，所以 TextLayout 与渲染代码不关心具体是哪一种。

#include <memory>
#include <string>
#include <string_view>

namespace kiriyama::render {

/// 字体在一个字号下的垂直度量。baseline 之上为 ascent，之下为 descent。
struct FontMetrics {
    double pixelSize{8.0};
    double ascent{8.0};
    double descent{0.0};
    double lineGap{0.0};

    /// 单行默认行高。调用方可以用 TextStyle::lineHeight 覆盖。
    [[nodiscard]] constexpr double lineHeight() const noexcept {
        return ascent + descent + lineGap;
    }
};

/// 字体面：与字号无关的字形来源。
class FontFace {
public:
    virtual ~FontFace() = default;

    [[nodiscard]] virtual std::string_view family() const noexcept = 0;

    /// 该字号下的垂直度量。
    [[nodiscard]] virtual FontMetrics metrics(double pixelSize) const noexcept = 0;

    /// 单个码点的前进宽度（不含字距调整）。
    [[nodiscard]] virtual double advance(char32_t codepoint, double pixelSize) const noexcept = 0;

    /// 是否真的有这个字形。false 表示渲染层会用兜底字形（方框）。
    [[nodiscard]] virtual bool supports(char32_t codepoint) const noexcept = 0;
};

/// 字体句柄：字体面 + 字号。拷贝很便宜（共享字体面）。
class Font {
public:
    /// 默认：内置位图字体，12px。
    Font() noexcept;
    Font(std::shared_ptr<const FontFace> face, double pixelSize);

    /// 内置位图字体（SDL 调试字体）。单例，可以放心长期持有。
    [[nodiscard]] static std::shared_ptr<const FontFace> builtinFace();

    [[nodiscard]] const FontFace& face() const noexcept { return *face_; }
    [[nodiscard]] double size() const noexcept { return size_; }
    void setSize(double pixelSize) noexcept;

    [[nodiscard]] FontMetrics metrics() const noexcept { return face_->metrics(size_); }

    [[nodiscard]] double advance(char32_t codepoint) const noexcept {
        return face_->advance(codepoint, size_);
    }

    /// 单行文本宽度。换行符按 0 宽处理，多行请用 TextLayout。
    [[nodiscard]] double measure(std::string_view utf8) const noexcept;

    /// 同样的字体面、不同字号。
    [[nodiscard]] Font withSize(double pixelSize) const;

private:
    std::shared_ptr<const FontFace> face_;
    double size_{12.0};
};

/// SDL3 内置 8×8 点阵字体（兜底实现）。
///
/// 度量：字形格 8×8，按 size/8 整体缩放。因此字号 s 下：
///   advance = s，ascent = s，descent = 0，lineGap = 0.25s（默认行高 1.25 倍）。
/// 只支持 ASCII 32~126；其余码点按等宽占位处理，渲染层会画成方框。
class SdlDebugFontFace final : public FontFace {
public:
    [[nodiscard]] std::string_view family() const noexcept override { return "SDL Debug 8x8"; }

    [[nodiscard]] FontMetrics metrics(double pixelSize) const noexcept override;

    [[nodiscard]] double advance(char32_t codepoint, double pixelSize) const noexcept override;

    [[nodiscard]] bool supports(char32_t codepoint) const noexcept override;
};

}  // namespace kiriyama::render
