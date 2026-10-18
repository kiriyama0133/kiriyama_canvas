#include "kiriyama/render/TooltipRenderer.h"

#include <algorithm>
#include <utility>

namespace kiriyama::render {

using core::RectD;
using core::Vec2d;

TooltipRenderer::TooltipRenderer(Painter& painter, SdlTextRenderer& text) noexcept
    : painter_(painter), text_(text) {
    options_.cornerRadius = style_.radius;
}

TooltipRenderer& TooltipRenderer::setFace(std::shared_ptr<const FontFace> face) {
    face_ = std::move(face);
    return *this;
}

TooltipRenderer& TooltipRenderer::setStyle(TooltipStyle style) {
    style_ = std::move(style);
    // 画出来的圆角以 style 为准，定位那边跟着同步，箭头才不会画进圆角里。
    options_.cornerRadius = style_.radius;
    return *this;
}

TooltipRenderer& TooltipRenderer::setOptions(core::TooltipOptions options) {
    options_ = options;
    options_.cornerRadius = style_.radius;
    return *this;
}

double TooltipRenderer::innerWidth() const noexcept {
    return std::max(0.0, style_.maxWidth - style_.paddingX * 2.0);
}

Font TooltipRenderer::titleFont() const {
    std::shared_ptr<const FontFace> face = face_ ? face_ : text_.defaultFace();
    if (!face) {
        face = Font::builtinFace();
    }
    return Font(std::move(face), style_.titleSize);
}

Font TooltipRenderer::bodyFont() const {
    std::shared_ptr<const FontFace> face = face_ ? face_ : text_.defaultFace();
    if (!face) {
        face = Font::builtinFace();
    }
    return Font(std::move(face), style_.bodySize);
}

TooltipRenderer::ContentLayout TooltipRenderer::layoutContent(const TooltipContent& content) const {
    ContentLayout out;
    const double inner = innerWidth();

    if (!content.title.empty()) {
        TextLayoutOptions options;
        options.maxWidth = inner;
        options.wrap = false;
        options.maxLines = 1;
        options.ellipsis = "...";
        out.title = layoutText(content.title, titleFont(), options);
        out.hasTitle = !out.title.empty();
    }

    if (!content.body.empty()) {
        TextLayoutOptions options;
        options.maxWidth = inner;
        options.wrap = true;
        options.maxLines = style_.maxBodyLines;
        options.ellipsis = "...";
        out.body = layoutText(content.body, bodyFont(), options);
        out.hasBody = !out.body.empty();
    }

    return out;
}

Vec2d TooltipRenderer::measure(const TooltipContent& content) const {
    const ContentLayout layout = layoutContent(content);
    if (!layout.hasTitle && !layout.hasBody) {
        return Vec2d{};
    }

    double width = 0.0;
    double height = 0.0;
    if (layout.hasTitle) {
        width = std::max(width, layout.title.width);
        height += layout.title.height;
    }
    if (layout.hasBody) {
        width = std::max(width, layout.body.width);
        if (layout.hasTitle) {
            height += style_.titleGap;
        }
        height += layout.body.height;
    }

    return Vec2d{width + style_.paddingX * 2.0, height + style_.paddingY * 2.0};
}

RectD TooltipRenderer::draw(const RectD& anchor, const TooltipContent& content,
                            const RectD& viewport, double opacity) {
    const Vec2d size = measure(content);
    const core::TooltipLayout layout =
        core::computeTooltipLayout(anchor, size, viewport, options_);
    drawLayout(layout, content, opacity);
    return layout.rect;
}

void TooltipRenderer::drawLayout(const core::TooltipLayout& layout, const TooltipContent& content,
                                 double opacity) {
    if (!layout.visible || opacity <= 0.0) {
        return;
    }

    const RectD& rect = layout.rect;
    // 卡片比两倍圆角还矮时要收半径，否则 SDL 的圆角矩形会画成一坨。
    const double radius = std::min(style_.radius, std::min(rect.width, rect.height) * 0.5);

    Vec2d triangle[3]{};
    const bool hasArrow = core::tooltipArrowPolygon(layout, options_.arrowWidth, triangle);

    // 1) 投影：纯色分层，越远越淡，近似一层柔光。
    //    SDL 没有 blur，三层偏移是这点预算内最像影子的做法。
    //    箭头也要跟着投，否则卡片下方的影带会在箭头处被"咬"出一个缺口。
    if (style_.shadow) {
        const double offsets[] = {2.0, 4.0, 7.0};
        const double alphas[] = {0.55, 0.35, 0.20};
        for (int i = 0; i < 3; ++i) {
            const double dy = offsets[i];
            painter_.color(style_.shadowColor, opacity * alphas[i])
                .fillRoundRect(RectD{rect.x, rect.y + dy, rect.width, rect.height}, radius);
            if (hasArrow) {
                const Vec2d shadowArrow[3] = {
                    Vec2d{triangle[0].x, triangle[0].y + dy},
                    Vec2d{triangle[1].x, triangle[1].y + dy},
                    Vec2d{triangle[2].x, triangle[2].y + dy},
                };
                painter_.fillPolygon(shadowArrow);
            }
        }
    }

    // 2) 卡片 + 箭头一起填背景色（箭头先填，卡片再盖住接缝）。
    painter_.color(style_.background, opacity).fillRoundRect(rect, radius);
    if (hasArrow) {
        painter_.color(style_.background, opacity).fillPolygon(triangle);
    }

    // 3) 卡片描边。
    painter_.color(style_.border, opacity).strokeWidth(1.0).strokeRoundRect(rect, radius);

    // 4) 描边会横穿箭头底边，把箭头再填一次盖掉，然后单独补箭头的两条斜边。
    if (hasArrow) {
        painter_.color(style_.background, opacity).fillPolygon(triangle);
        painter_.color(style_.border, opacity)
            .line(triangle[1], triangle[0])
            .line(triangle[0], triangle[2]);
    }

    // 5) 文字。
    //
    // 这里**不能**用 rect.width - 2 * paddingX 重新排版：rect 是按最宽行算出来的，
    // 两者只差零点几像素时浮点比较会让最后一个字掉到下一行，气泡就多出一行。
    // 用 measure 时的同一个 innerWidth 才稳。
    const ContentLayout textLayout = layoutContent(content);
    const double x = rect.x + style_.paddingX;
    double y = rect.y + style_.paddingY;

    if (textLayout.hasTitle) {
        TextLayoutOptions options;
        options.maxWidth = innerWidth();
        options.wrap = false;
        options.maxLines = 1;
        text_.drawText(content.title, titleFont(), Vec2d{x, y}, options, style_.titleColor, opacity);
        y += textLayout.title.height + style_.titleGap;
    }

    if (textLayout.hasBody) {
        TextLayoutOptions options;
        options.maxWidth = innerWidth();
        options.wrap = true;
        options.maxLines = style_.maxBodyLines;
        text_.drawText(content.body, bodyFont(), Vec2d{x, y}, options, style_.bodyColor, opacity);
    }

    painter_.reset();
}

}  // namespace kiriyama::render
