#include "kiriyama/render/PieChartRenderer.h"

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "kiriyama/core/Geometry.h"
#include "kiriyama/render/TextRenderer.h"

namespace kiriyama::render {

using core::Color;
using core::PieSector;
using core::Vec2d;

PieChartRenderer::PieChartRenderer(Painter& painter, SdlTextRenderer& text)
    : painter_(painter), text_(text) {}

std::string PieChartRenderer::labelText(const scene::PieSlice& slice) const {
    std::string text = slice.label();
    if (style_.showValues) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), " %.1f%%", slice.fraction() * 100.0);
        text += buffer;
    }
    return text;
}

void PieChartRenderer::drawLabel(const scene::PieChart& chart, const scene::PieSlice& slice,
                                 const PieSector& sector) {
    const std::string text = labelText(slice);
    if (text.empty()) {
        return;
    }

    const Vec2d origin = chart.frame().origin();
    const Vec2d anchor{sector.labelAnchor.x + origin.x, sector.labelAnchor.y + origin.y};

    const Font font(text_.defaultFace() ? text_.defaultFace() : Font::builtinFace(),
                    style_.labelFontSize);
    const TextLayoutOptions measureOptions;
    const TextLayout layout = text_.measure(text, font, measureOptions);

    // 按 midAngle 的水平分量决定文本往哪边延伸：左侧的标签向左、右侧向右，
    // 长标签才不会横着伸进饼图、压在扇区颜色上（和 SceneBuilder 同一套分档）。
    // 统一用左上角定位，省掉对齐语义的换算。
    const double cosine = std::cos(sector.midAngle);
    double originX = anchor.x - layout.boxWidth * 0.5;  // 中间：居中
    if (cosine > 0.2) {
        originX = anchor.x - 4.0;                       // 右侧标签：向右延伸
    } else if (cosine < -0.2) {
        originX = anchor.x + 4.0 - layout.boxWidth;     // 左侧标签：向左延伸
    }

    TextLayoutOptions alignLeft;
    alignLeft.align = core::TextAlign::Left;
    (void)text_.drawText(text, font, Vec2d{originX, anchor.y - layout.height * 0.5}, alignLeft,
                         style_.labelColor);
}

void PieChartRenderer::draw(const scene::PieChart& chart) {
    if (!chart.valid()) {
        return;
    }

    const core::PieLayout& pie = chart.layout();
    const Vec2d origin = chart.frame().origin();

    // 先画所有扇形，再画所有标签：标签要压在所有内容之上，
    // 否则后画扇区会盖住先画扇区伸过来的标签。
    for (std::size_t i = 0; i < chart.sliceCount(); ++i) {
        const scene::PieSlice* slice = chart.sliceAt(i);
        if (slice == nullptr) {
            continue;
        }
        const PieSector& sector = pie.sectors[i];

        // hover 外扩 = 外半径放大（圆心与角度边界不动，相邻扇区不露底）。
        // 复制一份布局把 outerRadius 抬高，sectorPolygon 直接给出放大后的顶点。
        const double radius = slice->explodeRadius();
        if (radius > 0.0) {
            core::PieLayout grown = pie;
            grown.outerRadius += radius;
            std::vector<Vec2d> points = core::sectorPolygon(sector, grown, chart.segments());
            for (Vec2d& point : points) {
                point = Vec2d{point.x + origin.x, point.y + origin.y};
            }
            painter_.color(slice->fillColor()).fillPolygon(points);
        } else {
            std::vector<Vec2d> points = core::sectorPolygon(sector, pie, chart.segments());
            for (Vec2d& point : points) {
                point = Vec2d{point.x + origin.x, point.y + origin.y};
            }
            painter_.color(slice->fillColor()).fillPolygon(points);
        }

        // 缝隙描边（默认关）。hover 外扩时缝隙线会跟着动、在相邻扇区边缘扫出
        // 底色残影，所以它只能用于静态图。
        if (style_.gapColor.visible() && style_.gapWidth > 0.0) {
            std::vector<Vec2d> gapPoints = core::sectorPolygon(sector, pie, chart.segments());
            for (Vec2d& point : gapPoints) {
                point = Vec2d{point.x + origin.x, point.y + origin.y};
            }
            painter_.color(style_.gapColor).strokeWidth(style_.gapWidth).polyline(gapPoints, true);
        }
    }

    if (style_.showLabels) {
        for (std::size_t i = 0; i < chart.sliceCount(); ++i) {
            const scene::PieSlice* slice = chart.sliceAt(i);
            if (slice != nullptr) {
                drawLabel(chart, *slice, pie.sectors[i]);
            }
        }
    }
}

}  // namespace kiriyama::render
