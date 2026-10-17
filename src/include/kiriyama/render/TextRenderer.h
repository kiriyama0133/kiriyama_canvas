#pragma once

// SDL 后端的文本渲染器。
//
// 职责只有一件：把 TextLayout 的结果按行画到 SDL_Renderer 上，并额外提供
// "直接画一棵场景树里的 Text 节点"的便利入口。排版逻辑全在 TextLayout，
// 这里不做任何断行 / 对齐计算。
//
// 关于字形：字体面决定走哪条路径。
//   - TtfFontFace（推荐）：SDL_ttf 光栅化，每个字形缓存一张带 alpha 的纹理，
//     绘制时用 color mod 上色，所以同一字形可以复用于任意颜色/透明度。
//   - SdlDebugFontFace（兜底）：SDL3 自带 8×8 点阵，靠 SDL_SetRenderScale
//     整块放大，只能 ASCII。没有可用的 TTF 时仍然能出图。
//
// 换字体不需要改这里：Font 自带 FontFace，按 face 的类型分派即可。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <SDL3/SDL.h>

#include "Font.h"
#include "SdlColor.h"
#include "TextLayout.h"
#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/SceneNode.h"

namespace kiriyama::render {

class SdlTextRenderer {
public:
    explicit SdlTextRenderer(SDL_Renderer* renderer);
    ~SdlTextRenderer();

    SdlTextRenderer(const SdlTextRenderer&) = delete;
    SdlTextRenderer& operator=(const SdlTextRenderer&) = delete;

    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return renderer_; }
    [[nodiscard]] bool valid() const noexcept { return renderer_ != nullptr; }

    /// 场景树里的 Text 节点只有 fontSize，字体面得从外面给。
    /// 不设置时用内置点阵字体。
    void setDefaultFace(std::shared_ptr<const FontFace> face);
    [[nodiscard]] const std::shared_ptr<const FontFace>& defaultFace() const noexcept {
        return defaultFace_;
    }

    /// 纯排版，不画。给调用方预留空间 / 做碰撞检测用。
    [[nodiscard]] TextLayout measure(std::string_view text, const Font& font,
                                     const TextLayoutOptions& options) const;

    /// 把文本画在 origin（文本块左上角，逻辑坐标）。返回实际绘制的外接矩形。
    core::RectD drawText(std::string_view text, const Font& font, Vec2d origin,
                         const TextLayoutOptions& options, const core::Color& color,
                         double opacity = 1.0);

    /// 画一个 Text 节点：字号 / 行高 / 对齐 / 基线全部取自 node.style()，
    /// 换行宽度取自 node.frame().width。
    void drawNode(const core::SceneNode& node, double inheritedOpacity = 1.0);

    /// 遍历整棵树，画所有可见 Text 节点。父节点 opacity 逐层相乘。
    /// 注意：文本不支持旋转（点阵字形是轴对齐贴图），只会应用平移分量。
    void drawScene(const core::SceneNode& root);

    /// 本次实例累计绘制的行数 / 字形数，方便在 demo 里看开销。
    [[nodiscard]] std::uint64_t lineCount() const noexcept { return lineCount_; }
    [[nodiscard]] std::uint64_t glyphCount() const noexcept { return glyphCount_; }
    void resetCounters() noexcept {
        lineCount_ = 0;
        glyphCount_ = 0;
    }

    /// 当前缓存了多少个字形纹理；调字号 / 换文案时看它的走势。
    [[nodiscard]] std::size_t glyphCacheSize() const noexcept;

    /// 像素吸附（默认开）。
    ///
    /// 字形是 1:1 的位图贴图，一旦落在分数像素上，采样器就要做一次线性插值，
    /// 笔画边缘会被抹成灰阶，小字号下尤其明显（看起来"发虚 + 字重变浅"）。
    /// TextLayout 算出的 baseline 往往是 11.5 这种半像素，所以这里统一把
    /// 基线取整、每个字形再各自吸到整像素。
    ///
    /// 关掉它只在需要把文本画到缩放后的坐标系里才有意义（比如整块 UI 缩放动画）。
    SdlTextRenderer& setPixelSnap(bool enabled) noexcept {
        pixelSnap_ = enabled;
        return *this;
    }
    [[nodiscard]] bool pixelSnap() const noexcept { return pixelSnap_; }

private:
    struct GlyphCache;
    struct LineCache;

    void drawLayout(const TextLayout& layout, Vec2d origin, const Font& font,
                    const core::Color& color, double opacity, double letterSpacing);
    void drawBitmapLayout(const TextLayout& layout, Vec2d origin, const Font& font,
                          const core::Color& color, double opacity, double letterSpacing);
    void drawTrueTypeLayout(const TextLayout& layout, Vec2d origin, const Font& font,
                            const core::Color& color, double opacity, double letterSpacing);
    /// 逐字排（只有需要额外字距时才走这条路）。
    void drawTrueTypeGlyphRun(const TextLayout& layout, Vec2d origin, const Font& font,
                              const core::Color& color, double opacity, double letterSpacing);

    SDL_Renderer* renderer_{nullptr};
    std::shared_ptr<const FontFace> defaultFace_;
    std::unique_ptr<GlyphCache> glyphs_;
    std::unique_ptr<LineCache> lines_;
    bool pixelSnap_{true};
    std::uint64_t lineCount_{0};
    std::uint64_t glyphCount_{0};
};

}  // namespace kiriyama::render
