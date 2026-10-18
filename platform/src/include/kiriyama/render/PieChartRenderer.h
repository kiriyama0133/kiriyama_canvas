#pragma once

// 饼图渲染器：把 scene::PieChart 的扇区画出来。
//
// 分工延续 TooltipHost / TooltipRenderer 的惯例——
// scene 层（PieChart / PieSlice）持有全部状态与几何（命中、hover、外扩进度、气泡），
// 这里只负责"落笔"：扇形多边形填充、缝隙描边、hover 描边、外侧标签。
//
// 坐标：扇区 arc 存在 PieChart 的父坐标系（见 PieChart.h），本渲染器假定
// chart 没有祖先变换（宿主里 chart 直接挂在逻辑空间根上，本地 == 世界）。
// 需要平移整个图表时改 PieChart::setFrame，不要给外层节点套 transform。
//
// 细分多边形复用 core::sectorPolygon（SceneBuilder 建描述树用的是同一份），
// 保证"命中判定的形状"（ArcD 精确角度）和"画出来的形状"（多边形逼近）来自同一套参数。

#include <string>

#include "kiriyama/render/Font.h"
#include "kiriyama/render/Painter.h"
#include "kiriyama/scene/PieChart.h"

namespace kiriyama::render {

class SdlTextRenderer;

struct PieStyle {
    /// 扇区外侧标签。关掉就只画饼。
    bool showLabels{true};
    /// 标签里带百分比（"Label 25.0%"）。只在 showLabels 打开时生效。
    bool showValues{true};
    double labelFontSize{12.5};
    core::Color labelColor{core::Color::Hex(0x5A5A54)};

    /// 扇区之间的缝：用这个颜色沿多边形描一圈。**默认关（宽 0）**——
    /// hover 外扩时缝隙线跟着动，会在相邻扇区边缘扫出白色残影（"闪白"）。
    /// 真要留缝再配 gapWidth + gapColor。
    core::Color gapColor{core::Color::Hex(0xFFFFFF)};
    double gapWidth{0.0};
};

class PieChartRenderer {
public:
    PieChartRenderer(Painter& painter, SdlTextRenderer& text);

    PieChartRenderer(const PieChartRenderer&) = delete;
    PieChartRenderer& operator=(const PieChartRenderer&) = delete;

    void setStyle(const PieStyle& style) { style_ = style; }
    [[nodiscard]] const PieStyle& style() const noexcept { return style_; }

    /// 画整张饼：扇形 + 缝隙 + hover 描边 + 标签。放在 tickTree 之后调用。
    void draw(const scene::PieChart& chart);

private:
    /// 标签的显示串（label + 可选百分比）。
    [[nodiscard]] std::string labelText(const scene::PieSlice& slice) const;
    void drawLabel(const scene::PieChart& chart, const scene::PieSlice& slice,
                   const core::PieSector& sector);

    Painter& painter_;
    SdlTextRenderer& text_;
    PieStyle style_{};
};

}  // namespace kiriyama::render
