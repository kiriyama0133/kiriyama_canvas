#include "kiriyama/render/TextRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

#include "kiriyama/render/TtfFont.h"
#include "kiriyama/render/TextLayout.h"
#include "kiriyama/render/Utf8.h"

namespace kiriyama::render {
namespace {

constexpr double kDebugCellSize = 8.0;  // SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE

[[nodiscard]] constexpr double clamp01(double value) noexcept {
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

}  // namespace

// ---------------------------------------------------------------- 字形缓存

/// TTF 字形 → 纹理。key 不含颜色：纹理统一用白色渲染，
/// 绘制时靠 color/alpha mod 上色，所以同一字形能被任意颜色复用。
struct SdlTextRenderer::GlyphCache {
    struct Entry {
        SDL_Texture* texture{nullptr};
        float width{0.0f};
        float height{0.0f};
        float bearingX{0.0f};  // 位图左边缘相对笔位
        float bearingY{0.0f};  // 位图顶边相对基线（向上为正）
    };

    struct Key {
        const FontFace* face{nullptr};
        int sizeKey{0};
        char32_t codepoint{0};

        [[nodiscard]] bool operator==(const Key& other) const noexcept {
            return face == other.face && sizeKey == other.sizeKey &&
                   codepoint == other.codepoint;
        }
    };

    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
            auto mix = [](std::size_t seed, std::size_t value) {
                return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
            };
            std::size_t h = reinterpret_cast<std::uintptr_t>(key.face);
            h = mix(h, static_cast<std::size_t>(key.sizeKey));
            h = mix(h, static_cast<std::size_t>(key.codepoint));
            return h;
        }
    };

    SDL_Renderer* renderer{nullptr};
    std::unordered_map<Key, Entry, KeyHash> entries;
    std::size_t budget{4096};

    ~GlyphCache() { clear(); }

    void clear() {
        for (auto& item : entries) {
            if (item.second.texture != nullptr) {
                SDL_DestroyTexture(item.second.texture);
            }
        }
        entries.clear();
    }

    const Entry& get(const TtfFontFace& face, char32_t codepoint, double pixelSize) {
        const Key key{&face, TtfFontFace::sizeKey(pixelSize), codepoint};
        if (const auto it = entries.find(key); it != entries.end()) {
            return it->second;
        }

        // 图表里的字形集合是收敛的，真撞上上限就整块丢掉重来：
        // 这比维护 LRU 便宜，而且只在切换大量字号时才会发生。
        if (entries.size() >= budget) {
            clear();
        }

        Entry entry{};
        if (TTF_Font* font = face.handleFor(pixelSize)) {
            int minx = 0;
            int maxx = 0;
            int miny = 0;
            int maxy = 0;
            int advance = 0;
            if (TTF_GetGlyphMetrics(font, static_cast<Uint32>(codepoint), &minx, &maxx, &miny, &maxy,
                                    &advance)) {
                entry.bearingX = static_cast<float>(minx);
                entry.bearingY = static_cast<float>(maxy);

                // 注意别用 TTF_RenderGlyph_Blended：那个 API 其实走的是"渲染一行
                // 单字文本"，返回的表面是整行盒子（宽到字形右边界、高是 font->height
                // 加顶部 bearing），字形只是盒子左上角的一小块，还带一圈空白。
                // TTF_GetGlyphImage 给的是紧凑位图，宽高恰好等于上面的
                // sz_width / sz_rows，一个纹素对应一个像素，贴图才是 1:1 的。
                SDL_Surface* surface =
                    TTF_GetGlyphImage(font, static_cast<Uint32>(codepoint), nullptr);
                if (surface != nullptr) {
                    // 空格这类空白字形没有可画的位图，但 advance 仍要生效。
                    if (surface->w > 1 && surface->h > 1) {
                        entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
                        if (entry.texture != nullptr) {
                            SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
                            // 1:1 贴图不需要滤波；留着 LINEAR 的话，只要落点带上
                            // 半个像素就会被插值一次，字形立刻发虚。
                            SDL_SetTextureScaleMode(entry.texture, SDL_SCALEMODE_NEAREST);
                            entry.width = static_cast<float>(surface->w);
                            entry.height = static_cast<float>(surface->h);
                        }
                    }
                    SDL_DestroySurface(surface);
                }
            }
        }

        return entries.emplace(key, entry).first->second;
    }
};

// ---------------------------------------------------------------- 行缓存

/// 整行 → 纹理。默认走这条路：SDL_ttf 内部按 26.6 定点推进、会做 kerning，
/// 字距比"自己按整数 advance 累加"均匀得多，也不会因为逐字取整而把一行
/// 越排越歪。纹理同样是白色，颜色靠 color mod。
///
/// 只有需要额外字距（letterSpacing != 0）时才退回逐字模式，见 GlyphCache。
struct SdlTextRenderer::LineCache {
    struct Entry {
        SDL_Texture* texture{nullptr};
        float width{0.0f};
        float height{0.0f};
    };

    struct Key {
        const FontFace* face{nullptr};
        int sizeKey{0};
        std::string text;

        [[nodiscard]] bool operator==(const Key& other) const noexcept {
            return face == other.face && sizeKey == other.sizeKey && text == other.text;
        }
    };

    struct KeyHash {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
            std::size_t h = reinterpret_cast<std::uintptr_t>(key.face);
            h ^= static_cast<std::size_t>(key.sizeKey) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            for (const unsigned char byte : key.text) {
                h ^= byte;
                h *= 1099511628211ull;  // FNV-1a
            }
            return h;
        }
    };

    SDL_Renderer* renderer{nullptr};
    std::unordered_map<Key, Entry, KeyHash> entries;
    std::size_t budget{512};  // 一行一张纹理，上限按条数卡住就够

    ~LineCache() { clear(); }

    void clear() {
        for (auto& item : entries) {
            if (item.second.texture != nullptr) {
                SDL_DestroyTexture(item.second.texture);
            }
        }
        entries.clear();
    }

    const Entry& get(const TtfFontFace& face, std::string_view text, double pixelSize) {
        Key key{&face, TtfFontFace::sizeKey(pixelSize), std::string{text}};
        if (const auto it = entries.find(key); it != entries.end()) {
            return it->second;
        }
        if (entries.size() >= budget) {
            clear();
        }

        Entry entry{};
        if (TTF_Font* font = face.handleFor(pixelSize)) {
            SDL_Surface* surface = TTF_RenderText_Blended(font, key.text.c_str(), key.text.size(),
                                                          SDL_Color{255, 255, 255, 255});
            if (surface != nullptr) {
                if (surface->w > 0 && surface->h > 0) {
                    entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
                    if (entry.texture != nullptr) {
                        SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
                        SDL_SetTextureScaleMode(entry.texture, SDL_SCALEMODE_NEAREST);
                        entry.width = static_cast<float>(surface->w);
                        entry.height = static_cast<float>(surface->h);
                    }
                }
                SDL_DestroySurface(surface);
            }
        }

        return entries.emplace(std::move(key), entry).first->second;
    }
};

// ---------------------------------------------------------------- 生命周期

SdlTextRenderer::SdlTextRenderer(SDL_Renderer* renderer)
    : renderer_(renderer),
      defaultFace_(Font::builtinFace()),
      glyphs_(std::make_unique<GlyphCache>()),
      lines_(std::make_unique<LineCache>()) {
    glyphs_->renderer = renderer;
    lines_->renderer = renderer;
}

SdlTextRenderer::~SdlTextRenderer() = default;

void SdlTextRenderer::setDefaultFace(std::shared_ptr<const FontFace> face) {
    defaultFace_ = face ? std::move(face) : Font::builtinFace();
}

std::size_t SdlTextRenderer::glyphCacheSize() const noexcept {
    const std::size_t glyphs = glyphs_ ? glyphs_->entries.size() : 0;
    const std::size_t lines = lines_ ? lines_->entries.size() : 0;
    return glyphs + lines;
}

// ---------------------------------------------------------------- 对外接口

TextLayout SdlTextRenderer::measure(std::string_view text, const Font& font,
                                    const TextLayoutOptions& options) const {
    return layoutText(text, font, options);
}

core::RectD SdlTextRenderer::drawText(std::string_view text, const Font& font, Vec2d origin,
                                      const TextLayoutOptions& options, const core::Color& color,
                                      double opacity) {
    const TextLayout layout = layoutText(text, font, options);
    if (!renderer_ || layout.empty()) {
        return {};
    }
    drawLayout(layout, origin, font, color, opacity, options.letterSpacing);
    return core::RectD{origin.x, origin.y, layout.boxWidth, layout.height};
}

void SdlTextRenderer::drawNode(const core::SceneNode& node, double inheritedOpacity) {
    if (!renderer_ || node.type() != core::NodeType::Text || !node.visible()) {
        return;
    }
    const core::SceneStyle& style = node.style();
    const double opacity = inheritedOpacity * clamp01(style.opacity);
    if (opacity <= 0.0 || !style.fill.visible() || node.text().empty()) {
        return;
    }

    const core::RectD& frame = node.frame();
    TextLayoutOptions options;
    options.maxWidth = frame.width > 0.0 ? frame.width : 0.0;
    options.wrap = options.maxWidth > 0.0;
    options.align = style.textAlign;
    options.lineHeight = style.lineHeight;

    Font font(defaultFace_ ? defaultFace_ : Font::builtinFace(),
              style.fontSize > 0.0 ? style.fontSize : 12.0);
    const TextLayout layout = layoutText(node.text(), font, options);
    if (layout.empty()) {
        return;
    }

    // 文本只能平移：字形是轴对齐贴图，旋转 / 斜切会被忽略。
    const Vec2d origin = node.worldTransform().apply(placeTextBox(frame, layout, style.textBaseline));
    drawLayout(layout, origin, font, style.fill, opacity, options.letterSpacing);
}

void SdlTextRenderer::drawScene(const core::SceneNode& root) {
    if (!renderer_) {
        return;
    }
    const std::function<void(const core::SceneNode&, double)> walk =
        [&](const core::SceneNode& node, double opacity) {
            if (!node.visible()) {
                return;
            }
            const double effective = opacity * clamp01(node.style().opacity);
            if (effective <= 0.0) {
                return;  // 整棵子树都不可见
            }
            if (node.type() == core::NodeType::Text) {
                drawNode(node, opacity);  // node 自身的 opacity 由 drawNode 再乘一次
            }
            for (const auto& child : node.children()) {
                if (child) {
                    walk(*child, effective);
                }
            }
        };
    walk(root, 1.0);
}

// ---------------------------------------------------------------- 绘制实现

void SdlTextRenderer::drawLayout(const TextLayout& layout, Vec2d origin, const Font& font,
                                 const core::Color& color, double opacity, double letterSpacing) {
    if (dynamic_cast<const TtfFontFace*>(&font.face()) != nullptr) {
        drawTrueTypeLayout(layout, origin, font, color, opacity, letterSpacing);
    } else {
        drawBitmapLayout(layout, origin, font, color, opacity, letterSpacing);
    }
}

void SdlTextRenderer::drawTrueTypeLayout(const TextLayout& layout, Vec2d origin, const Font& font,
                                         const core::Color& color, double opacity,
                                         double letterSpacing) {
    if (letterSpacing != 0.0) {
        drawTrueTypeGlyphRun(layout, origin, font, color, opacity, letterSpacing);
        return;
    }

    const SDL_Color tint = toSdlColor(color, opacity);
    if (tint.a == 0) {
        return;
    }

    const auto* face = dynamic_cast<const TtfFontFace*>(&font.face());
    if (face == nullptr || !lines_) {
        return;
    }

    // SDL_ttf 渲染出来的整行盒，基线固定在 surface 顶部往下 ascent 处
    // （TTF_Size_Internal 把 miny 初始化为 0、maxy 初始化为 font->height）。
    const double ascent = layout.metrics.ascent;

    for (const TextLine& line : layout.lines) {
        if (line.text.empty()) {
            continue;
        }

        const LineCache::Entry& run = lines_->get(*face, line.text, font.size());
        if (run.texture == nullptr) {
            ++lineCount_;
            glyphCount_ += codepointCount(line.text);
            continue;
        }

        double x = origin.x + line.x;
        double y = origin.y + line.baseline - ascent;
        if (pixelSnap_) {
            x = std::round(x);
            y = std::round(y);
        }

        const SDL_FRect destination{static_cast<float>(x), static_cast<float>(y), run.width,
                                    run.height};
        SDL_SetTextureColorMod(run.texture, tint.r, tint.g, tint.b);
        SDL_SetTextureAlphaMod(run.texture, tint.a);
        SDL_RenderTexture(renderer_, run.texture, nullptr, &destination);

        ++lineCount_;
        glyphCount_ += codepointCount(line.text);
    }
}

void SdlTextRenderer::drawTrueTypeGlyphRun(const TextLayout& layout, Vec2d origin, const Font& font,
                                           const core::Color& color, double opacity,
                                           double letterSpacing) {
    const SDL_Color tint = toSdlColor(color, opacity);
    if (tint.a == 0) {
        return;
    }

    const auto* face = dynamic_cast<const TtfFontFace*>(&font.face());
    if (face == nullptr || !glyphs_) {
        return;
    }

    for (const TextLine& line : layout.lines) {
        if (line.text.empty()) {
            continue;
        }

        // 整行共用一条吸附后的基线：行内所有字形相对位置保持不变，
        // 只是整行落到整像素上，不会出现"每个字各自抖动"。
        const double penStart = pixelSnap_ ? std::round(origin.x + line.x) : origin.x + line.x;
        const double baseline =
            pixelSnap_ ? std::round(origin.y + line.baseline) : origin.y + line.baseline;

        double pen = penStart;
        std::size_t offset = 0;
        while (offset < line.text.size()) {
            const DecodedCodepoint decoded = decodeUtf8(line.text, offset);
            const std::size_t bytes = decoded.bytes == 0 ? 1 : decoded.bytes;

            const GlyphCache::Entry& glyph = glyphs_->get(*face, decoded.value, font.size());
            if (glyph.texture != nullptr) {
                const double targetX = pen + static_cast<double>(glyph.bearingX);
                const double targetY = baseline - static_cast<double>(glyph.bearingY);
                const SDL_FRect destination{
                    static_cast<float>(pixelSnap_ ? std::round(targetX) : targetX),
                    static_cast<float>(pixelSnap_ ? std::round(targetY) : targetY),
                    glyph.width,
                    glyph.height,
                };
                SDL_SetTextureColorMod(glyph.texture, tint.r, tint.g, tint.b);
                SDL_SetTextureAlphaMod(glyph.texture, tint.a);
                SDL_RenderTexture(renderer_, glyph.texture, nullptr, &destination);
            }

            pen += font.advance(decoded.value) + letterSpacing;
            offset += bytes;
            ++glyphCount_;
        }
        ++lineCount_;
    }
}

void SdlTextRenderer::drawBitmapLayout(const TextLayout& layout, Vec2d origin, const Font& font,
                                       const core::Color& color, double opacity,
                                       double letterSpacing) {
    const double scale = std::max(font.size() / kDebugCellSize, 1.0 / 16.0);
    const bool rescale = std::abs(scale - 1.0) > 1e-9;
    if (rescale) {
        SDL_SetRenderScale(renderer_, static_cast<float>(scale), static_cast<float>(scale));
    }
    const double inverse = 1.0 / scale;

    for (const TextLine& line : layout.lines) {
        if (line.text.empty()) {
            continue;
        }
        const SDL_Color tint = toSdlColor(color, opacity);
        SDL_SetRenderDrawColor(renderer_, tint.r, tint.g, tint.b, tint.a);

        // 字形顶边 = 基线 - ascent，避免把行高的 half-leading 算进字形里
        const double rawX = origin.x + line.x;
        const double rawY = origin.y + line.baseline - layout.metrics.ascent;
        const double x = pixelSnap_ ? std::round(rawX) : rawX;
        const double y = pixelSnap_ ? std::round(rawY) : rawY;

        if (letterSpacing == 0.0) {
            SDL_RenderDebugText(renderer_, static_cast<float>(x * inverse),
                                static_cast<float>(y * inverse), line.text.c_str());
            glyphCount_ += codepointCount(line.text);
        } else {
            // 有额外字距：逐字符推进笔位
            double pen = x;
            std::size_t offset = 0;
            while (offset < line.text.size()) {
                const DecodedCodepoint decoded = decodeUtf8(line.text, offset);
                const std::size_t bytes = decoded.bytes == 0 ? 1 : decoded.bytes;
                const std::string glyph = line.text.substr(offset, bytes);
                SDL_RenderDebugText(renderer_, static_cast<float>(pen * inverse),
                                    static_cast<float>(y * inverse), glyph.c_str());
                pen += font.advance(decoded.value) + letterSpacing;
                offset += bytes;
                ++glyphCount_;
            }
        }
        ++lineCount_;
    }

    if (rescale) {
        SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
    }
}

}  // namespace kiriyama::render
