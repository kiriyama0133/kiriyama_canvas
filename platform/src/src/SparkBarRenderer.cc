#include "kiriyama/render/SparkBarRenderer.h"

#include <utility>

#include "kiriyama/render/TextRenderer.h"

namespace kiriyama::render {

using core::RectD;
using core::Vec2d;

SparkBarRenderer::SparkBarRenderer(Painter& painter, SdlTextRenderer& text)
    : painter_(painter), text_(text) {}

void SparkBarRenderer::draw(const scene::SparkBar& bar) {
    if (!bar.valid()) {
        return;
    }

    const core::SparkBarLayout& layout = bar.layout();
    const core::SparkBarData& data = bar.data();
    const core::SparkBarOptions& options = bar.options();
    const double radius = options.cornerRadius;

    // layout 的矩形已经是节点本地坐标（组件 rebuild 时平移过），这里直接画。
    for (const core::SparkBarItem& item : layout.items) {
        for (const core::SparkBarSegment& segment : item.segments) {
            painter_.color(bar.colorAt(segment.seriesIndex)).fillRoundRect(segment.rect, radius);
        }
    }

    // hover 描边单独一轮：压在所有填充之上，否则会被后画的相邻段盖住。
    // 颜色取这一段自己的颜色，圆角沿用柱的圆角 + 半个线宽（同心）。
    //
    // 注意用"外描边"：把描边路径外扩半个线宽，整条线就落在段的外侧。
    // 居中描边只往外长出 1px、同色叠在浅底上几乎看不出来——试过，等于没做。
    const std::size_t hoverItem = bar.hoveredItem();
    const std::size_t hoverSegment = bar.hoveredSegment();
    if (options.hoverBorderWidth > 0.0 && hoverItem < layout.items.size() &&
        hoverSegment != core::kInvalidIndex) {
        const core::SparkBarItem& item = layout.items[hoverItem];
        if (hoverSegment < item.segments.size()) {
            const core::SparkBarSegment& segment = item.segments[hoverSegment];
            const double half = options.hoverBorderWidth * 0.5;
            const Vec2d rectOrigin = segment.rect.origin();
            const core::RectD outline{rectOrigin.x - half, rectOrigin.y - half,
                                      segment.rect.width + options.hoverBorderWidth,
                                      segment.rect.height + options.hoverBorderWidth};
            painter_.color(core::Color::mix(bar.colorAt(segment.seriesIndex),
                                            core::Color::Rgb(0.0, 0.0, 0.0), 0.12))
                .strokeWidth(options.hoverBorderWidth)
                .strokeRoundRect(outline, radius + half);
        }
    }

    for (const core::SparkBarItem& item : layout.items) {
        if (!options.showTags || item.tagRect.empty()) {
            continue;
        }
        if (item.tagIndex >= data.tags.size()) {
            continue;
        }
        const std::string& tag = data.tags[item.tagIndex];
        if (tag.empty()) {
            continue;
        }

        const Font font(text_.defaultFace() ? text_.defaultFace() : Font::builtinFace(),
                        options.tagFontSize);
        const TextLayoutOptions measureOptions;
        const TextLayout textLayout = text_.measure(tag, font, measureOptions);

        // 标签水平居中在柱下，垂直居中在 tag 行里。
        const Vec2d center = item.tagRect.center();
        TextLayoutOptions align;
        align.align = core::TextAlign::Center;
        (void)text_.drawText(
            tag, font,
            Vec2d{center.x - textLayout.boxWidth * 0.5, center.y - textLayout.height * 0.5},
            align, options.tagColor);
    }
}

}  // namespace kiriyama::render
