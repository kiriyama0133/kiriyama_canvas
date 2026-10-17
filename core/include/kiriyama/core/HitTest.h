#pragma once

// 命中测试：把鼠标 / 触摸的像素坐标翻译回数据。
//
// 折线与散点命中折线本身（空值处断开，不会跨过缺口连线）；
// 柱状命中矩形（堆叠时后画的系列优先）；
// 饼图按半径 + 角度定位扇区；
// 类目轴提供"最近类别"，用于共享 tooltip。

#include <cstddef>
#include <limits>

#include "Data.h"
#include "Layout.h"

namespace kiriyama::core {

struct HitTestOptions {
    /// 命中容差（像素）。
    double tolerance{6.0};
};

struct HitResult {
    bool hit{false};
    std::size_t seriesIndex{kInvalidIndex};
    std::size_t pointIndex{kInvalidIndex};
    std::size_t categoryIndex{kInvalidIndex};
    std::size_t sliceIndex{kInvalidIndex};
    /// 到图元的最近距离（像素）；命中矩形/扇区时为 0。
    double distance{std::numeric_limits<double>::infinity()};
    /// 吸附后的像素位置（折线取最近顶点，类目取轴上的中心点）。
    Vec2d position{};
    double value{0.0};
    /// 饼图扇区占比，其余为 0。
    double fraction{0.0};
};

/// 命中折线 / 折线顶点吸附。返回最近的顶点索引与吸附位置。
[[nodiscard]] HitResult hitTestPolyline(const Dataset& dataset, const ChartLayout& layout,
                                        const Vec2d& pixel,
                                        const HitTestOptions& options = {});

/// 只命中数据顶点（散点图）。
[[nodiscard]] HitResult hitTestVertices(const Dataset& dataset, const ChartLayout& layout,
                                        const Vec2d& pixel,
                                        const HitTestOptions& options = {});

/// 命中柱形。堆叠时从后往前查，保证上层柱优先。
[[nodiscard]] HitResult hitTestBars(const Dataset& dataset, const ChartLayout& layout,
                                    BarMode mode, const Vec2d& pixel,
                                    const HitTestOptions& options = {});

/// 命中饼图扇区。圆心空洞不算命中。
[[nodiscard]] HitResult hitTestPie(const PieChartData& data, const PieLayout& layout,
                                   const Vec2d& pixel, const HitTestOptions& options = {});

/// 类目轴最近类别：指针在绘图区内即视为命中，distance 是到类别中心的水平距离。
[[nodiscard]] HitResult hitTestCategory(const Dataset& dataset, const ChartLayout& layout,
                                        const Vec2d& pixel);

/// 数值 x 最近的类别索引，数据集为空时返回 kInvalidIndex。
[[nodiscard]] std::size_t nearestCategoryIndex(const Dataset& dataset, double xValue);

}  // namespace kiriyama::core
