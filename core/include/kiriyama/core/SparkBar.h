#pragma once

// SparkStackBar：一条由若干段堆叠成的微型条形图，通常一排并列。
// 用在 KPI 卡片、表格内嵌趋势这类"没有坐标轴、只看构成与相对高低"的地方。
//
// 数据形状和表格一致——tag（类目）× series（堆叠层）：
//
//   series:  Revenue / Cost / Tax          （决定堆叠顺序与颜色）
//   tags:    2021 / 2022 / 2023 / 2024     （每条柱的类目，画在柱下面）
//   values:  values[tagIndex][seriesIndex] （NaN = 该段缺失，不画、不计入总量）
//
// 组件不持有数据：core 只定义模型与纯几何布局，渲染层照 SparkBarLayout 落笔。
// 负值 / NaN 段被跳过（堆叠图里负值没有意义，强行画会出门反效果）。

#include <cstddef>
#include <string>
#include <vector>

#include "Color.h"
#include "Geometry.h"

namespace kiriyama::core {

/// 一个堆叠层。color 默认 Alpha 0，由调用方填（渲染层按 series 顺序取）。
struct SparkBarSeries {
    std::string name;
    Color color{};
};

struct SparkBarData {
    std::vector<SparkBarSeries> series;
    std::vector<std::string> tags;
    /// values[tagIndex][seriesIndex]。行可以比列短——短的部分按缺失处理。
    std::vector<std::vector<double>> values;

    void clear();

    [[nodiscard]] bool empty() const noexcept { return tags.empty() || series.empty(); }
    [[nodiscard]] std::size_t tagCount() const noexcept { return tags.size(); }
    [[nodiscard]] std::size_t seriesCount() const noexcept { return series.size(); }

    /// 某条柱的总量：只累加有限正值。
    [[nodiscard]] double totalAt(std::size_t tagIndex) const noexcept;

    /// 所有柱里最大的总量（布局按它归一化高度）。空数据返回 0。
    [[nodiscard]] double maxTotal() const noexcept;

    /// 某段的值；缺失（越界 / NaN / 负数）返回 0。
    [[nodiscard]] double valueAt(std::size_t tagIndex, std::size_t seriesIndex) const noexcept;
};

// ---------------------------------------------------------------- 样式 / 布局

struct SparkBarOptions {
    /// 柱宽；<= 0 时按"可用宽度 - 间距"均分。
    double barWidth{0.0};
    /// 柱间距。
    double barGap{10.0};
    /// 堆叠段之间的缝隙（视觉上把层分开）。
    double segmentGap{1.5};
    /// 柱的圆角（顶段用完整圆角，中间段不圆角）。
    double cornerRadius{3.0};

    /// hover 时给命中的那一段描一圈"它自己的颜色"（沿同一圆角）。0 = 关闭。
    /// 描边是居中的，所以会向外长出半个线宽——视觉上那段更饱满、边界更实，
    /// 不会引入第二种颜色（饼图时代那个深灰描边就是反面教材）。
    double hoverBorderWidth{2.0};

    /// 柱下方 tag 行。
    bool showTags{true};
    double tagFontSize{11.0};
    Color tagColor{Color::Hex(0x6E6E68)};
    /// tag 文字与柱子的间距。
    double tagGap{6.0};
    /// tag 行高（0 = 按字号算）。
    double tagHeight{0.0};

    /// 柱子整体的最高值对应的像素高度占可用高度的比例。
    double heightRatio{1.0};
};

struct SparkBarSegment {
    /// 段矩形（已含段的缝隙）。
    RectD rect{};
    std::size_t seriesIndex{0};
    double value{0.0};
};

struct SparkBarItem {
    std::size_t tagIndex{0};
    /// 整条柱的外接矩形（所有段合起来）。
    RectD barRect{};
    /// 自下而上的堆叠段。
    std::vector<SparkBarSegment> segments{};
    /// tag 行矩形（showTags 打开时有效，否则为空）。
    RectD tagRect{};
};

struct SparkBarLayout {
    std::vector<SparkBarItem> items{};
    /// 有可画的柱时为 true。
    bool valid{false};
};

/// 纯几何：给定画布尺寸与选项，算出每条柱、每个堆叠段、tag 行的矩形。
/// 高度按 maxTotal 归一化；总量为 0 的柱只保留标签。
[[nodiscard]] SparkBarLayout computeSparkBarLayout(Vec2d size, const SparkBarData& data,
                                                   const SparkBarOptions& options = {});

}  // namespace kiriyama::core
