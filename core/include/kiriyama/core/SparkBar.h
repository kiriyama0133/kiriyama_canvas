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
    /// 横向条的厚度（高度，像素）；<= 0 时按可用高度自适应（默认单条约 14px，多条自动均分钳制）。
    double barWidth{0.0};
    /// 多条横向柱之间的纵向间距。
    double barGap{18.0};
    /// 横向堆叠段之间的缝隙（视觉上把不同语言层分开）。
    double segmentGap{2.0};
    /// 横向段圆角半径。
    double cornerRadius{5.0};

    /// hover 时给命中的那一段描一圈"它自己的颜色"（沿同一圆角）。0 = 关闭。
    double hoverBorderWidth{2.0};

    /// 是否显示行标题（tag）与底部 GitHub 风格圆点语言占比图例。
    bool showTags{true};
    double tagFontSize{12.0};
    Color tagColor{Color::Hex(0x5A5A54)};
    /// 行标题文字与横向条之间的间距。
    double tagGap{6.0};
    /// 行标题行高（0 = 按字号算）。
    double tagHeight{0.0};

    /// 内容区域占可用高度的比例（0..1）。
    double heightRatio{1.0};
};

struct SparkBarSegment {
    /// 横向段矩形（自左向右排列，已扣除段间缝隙）。
    RectD rect{};
    std::size_t seriesIndex{0};
    double value{0.0};
    /// 该段在当前行中的占比（0..1）。
    double fraction{0.0};
};

struct SparkBarItem {
    std::size_t tagIndex{0};
    /// 整条横向柱的外接矩形（左到右满宽）。
    RectD barRect{};
    /// 自左向右的堆叠段。
    std::vector<SparkBarSegment> segments{};
    /// 行标题矩形（showTags 打开时位于横向条上方，否则为空）。
    RectD tagRect{};
};

/// 底部 GitHub 风格图例项：彩色圆点 + "语言名 占比%"。
struct SparkBarLegendItem {
    std::size_t seriesIndex{0};
    double value{0.0};
    double fraction{0.0};
    RectD dotRect{};
    RectD textRect{};
    RectD hitRect{};
};

struct SparkBarLayout {
    std::vector<SparkBarItem> items{};
    std::vector<SparkBarLegendItem> legend{};
    /// 有可画的柱时为 true。
    bool valid{false};
};

/// 纯几何：给定画布尺寸与选项，算出每条横向百分比堆叠条、每个分段、行标题及底部图例的矩形。
/// 每条横向条内部按该行 totalAt(t) 100% 归一化（自左向右铺满）；总量为 0 的行只保留标签。
[[nodiscard]] SparkBarLayout computeSparkBarLayout(Vec2d size, const SparkBarData& data,
                                                   const SparkBarOptions& options = {});

}  // namespace kiriyama::core
