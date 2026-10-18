#pragma once

// 布局计算：数据边界 -> 刻度 -> padding -> 绘图区 -> 坐标轴。
//
// 两个入口：
//   computeLayout()    直角坐标系（折线 / 柱状），可选左右双 y 轴并对齐网格线。
//   computePieLayout() 饼图 / 甜甜圈，直接给出扇区角度与标签锚点。
//
// 文本宽度通过 TextMeasureFn 注入，默认实现按字符数估算（CJK 按全角算），
// 不依赖任何字体库，wasm 里可以直接跑；接入真实字体后替换回调即可。

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Axis.h"
#include "Data.h"
#include "Geometry.h"

namespace kiriyama::core {

// ---------------------------------------------------------------- 边距 / 文本

struct Padding {
    double top{16.0};
    double right{16.0};
    double bottom{16.0};
    double left{16.0};

    [[nodiscard]] static constexpr Padding All(double value) noexcept {
        return {value, value, value, value};
    }
    [[nodiscard]] static constexpr Padding Symmetric(double vertical,
                                                     double horizontal) noexcept {
        return {vertical, horizontal, vertical, horizontal};
    }
};

struct TextMetrics {
    double width{0.0};
    double height{0.0};
};

using TextMeasureFn = std::function<TextMetrics(std::string_view text, double fontSize)>;

/// 无字体环境下的估算：ASCII 约 0.6em，空格/标点 0.3em，UTF-8 多字节字符按 1em。
[[nodiscard]] TextMetrics defaultTextMeasure(std::string_view text, double fontSize);

// ---------------------------------------------------------------- 选项

struct AxisOptions {
    bool visible{true};
    ScaleType scale{ScaleType::Linear};
    /// 期望刻度数（实际值会被 nice 数调整）。
    int targetTickCount{5};
    /// 强制把 0 纳入 domain，柱状图 / 面积图基本都要开。
    bool includeZero{false};
    /// 把 domain 扩到刻度端点，标签干净。
    bool nice{true};
    /// 标签会重叠时自动减少刻度（或加大类别步长）。
    bool avoidLabelOverlap{true};
    std::string title;
    double labelFontSize{12.0};
    double titleFontSize{12.0};
    /// 标签与轴线的间距。
    double labelGap{6.0};
    TickFormat format{};
};

struct LayoutOptions {
    Padding padding{};
    YDirection yDirection{YDirection::Down};

    AxisOptions x{};
    AxisOptions y{};
    bool hasSecondaryY{false};
    AxisOptions secondaryY{};

    /// 非空时，x 轴按类别均分，标签直接取这里的字符串（柱状图用）。
    /// 为空则按数值轴处理。
    std::vector<std::string> categoryLabels;

    /// 右轴与绘图区的额外间距。
    double plotGap{10.0};
    double minPlotWidth{24.0};
    double minPlotHeight{24.0};

    /// 为空时用 defaultTextMeasure。
    TextMeasureFn measureText{};
};

// ---------------------------------------------------------------- 结果

struct TickMark {
    double value{0.0};
    double position{0.0};
    std::string label;
};

struct AxisLayout {
    Axis axis{};
    AxisPosition position{AxisPosition::Bottom};
    ScaleType scale{ScaleType::Linear};
    RangeD domain{0.0, 1.0};
    RangeD pixelRange{0.0, 1.0};
    std::vector<TickMark> ticks;
    double labelWidth{0.0};
    double labelHeight{0.0};
    std::string title;
    bool visible{true};

    [[nodiscard]] double map(double value) const noexcept { return axis.map(value); }
    [[nodiscard]] double invert(double pixel) const noexcept { return axis.invert(pixel); }
};

struct ChartLayout {
    RectD canvas{};
    /// 真正的绘图区（不含轴标签和 padding）。
    RectD plot{};
    AxisLayout xAxis{};
    AxisLayout yAxis{};
    AxisLayout secondaryYAxis{};
    bool hasSecondaryY{false};
    /// 画布放不下 minPlotWidth / minPlotHeight 时为 false。
    bool valid{false};

    /// 数据 -> 像素（使用左轴）。
    [[nodiscard]] Vec2d toPixel(double x, double y) const noexcept;
    [[nodiscard]] Vec2d toPixel(const Vec2d& dataPoint) const noexcept;
    /// 数据 -> 像素（使用右轴），用于第二数据系列。
    [[nodiscard]] Vec2d toPixelSecondaryY(double x, double y) const noexcept;
    /// 像素 -> 数据（使用左轴）。
    [[nodiscard]] Vec2d fromPixel(const Vec2d& pixel) const noexcept;

    [[nodiscard]] double toPixelX(double x) const noexcept;
    [[nodiscard]] double toPixelY(double y) const noexcept;
    [[nodiscard]] double fromPixelX(double pixel) const noexcept;
    [[nodiscard]] double fromPixelY(double pixel) const noexcept;
};

/// 左右 y 轴对齐结果：两轴 domain 覆盖各自数据，且分段数相同——网格线逐条重合。
struct AlignedDomains {
    RangeD primary{0.0, 1.0};
    RangeD secondary{0.0, 1.0};
    int intervals{1};
};

/// 让两个线性轴拥有同样的分段数。两轴 step 各自保持 nice，domain 向外对称扩展。
[[nodiscard]] AlignedDomains alignYDomains(RangeD primary, RangeD secondary,
                                           int targetTickCount = 5) noexcept;

// ---------------------------------------------------------------- 布局入口

[[nodiscard]] ChartLayout computeLayout(Vec2d canvasSize, const DataBounds& bounds,
                                        const LayoutOptions& options = {});

/// 双 y 轴版本：secondaryBounds 只用到 y 范围。
[[nodiscard]] ChartLayout computeLayout(Vec2d canvasSize, const DataBounds& bounds,
                                        const DataBounds& secondaryBounds,
                                        const LayoutOptions& options = {});

// ---------------------------------------------------------------- 柱形几何

struct BarOptions {
    /// 类别内两侧留白占类别带宽的比例。
    double groupPaddingRatio{0.2};
    /// 同类别内相邻柱之间的间隙比例。
    double barGapRatio{0.08};
    double maxBarWidth{56.0};
};

struct BarRect {
    std::size_t seriesIndex{0};
    std::size_t categoryIndex{0};
    RectD rect{};
    double value{0.0};
    /// 堆叠时的起始值（非堆叠恒为 0）。
    double baseValue{0.0};
};

/// 计算柱形屏幕矩形。柱状图 x 使用类别索引，domain 应为 [-0.5, n-0.5]。
/// 空值、零高度柱会被跳过。
[[nodiscard]] std::vector<BarRect> computeBarRects(const Dataset& dataset,
                                                   const ChartLayout& layout, BarMode mode,
                                                   const BarOptions& options = {});

// ---------------------------------------------------------------- 饼图布局

struct PieLayoutOptions {
    Padding padding{};
    /// 起始角，默认 -90°（12 点方向），顺时针展开。
    double startAngle{-math::kHalfPi};
    /// 内半径占比，> 0 即甜甜圈。
    double innerRadiusRatio{0.0};
    /// 外半径占可用区域短边的比例。
    double radiusRatio{0.85};
    /// 标签锚点离外圆的距离。
    double labelOffset{10.0};
};

struct PieSector {
    std::size_t sliceIndex{0};
    double value{0.0};
    double fraction{0.0};
    double startAngle{0.0};
    double sweepAngle{0.0};
    double midAngle{0.0};
    /// 扇区外侧的标签锚点。
    Vec2d labelAnchor{};
};

struct PieLayout {
    RectD canvas{};
    Vec2d center{};
    double innerRadius{0.0};
    double outerRadius{0.0};
    std::vector<PieSector> sectors;
    bool valid{false};
};

[[nodiscard]] PieLayout computePieLayout(Vec2d canvasSize, const PieChartData& data,
                                         const PieLayoutOptions& options = {});

/// 把一个扇区近似成多边形顶点（外弧 + 内弧，实心饼从圆心起）。
/// SceneBuilder 建描述树和渲染层画扇形共用这一份，保证"描的"和"画的"是同一个形状。
/// fullCircleSegments 是整圆细分数，单个扇区按弧长比例取用（至少 2 段）。
[[nodiscard]] std::vector<Vec2d> sectorPolygon(const PieSector& sector, const PieLayout& layout,
                                               int fullCircleSegments);

}  // namespace kiriyama::core
