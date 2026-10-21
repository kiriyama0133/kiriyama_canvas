#pragma once

// 折线图交互组件（scene 层）：
//   · LinePointNode（继承 TooltipTarget）：单个数据节点组件，位置由父组件 LineChart 决定，
//     自身独立维护命中判定（hitRadius）、Hover 视觉反馈（圆点放大 + 外圈光晕）以及 Tooltip 内容与锚点；
//   · LineChart（继承 Widget）：父组件专注于折线图业务（双轴刻度与文字样式/方向配置、
//     网格与折线/平滑曲线/面积描述树对账、计算并将每个 LinePointNode 摆放到对应的像素坐标）。

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "kiriyama/core/Data.h"
#include "kiriyama/core/Layout.h"
#include "kiriyama/core/SceneBuilder.h"
#include "kiriyama/scene/TooltipTarget.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

using core::Color;
using core::Paint;

/// 折线图数据节点（LinePointNode）的外观与交互参数。
struct LinePointStyle {
    bool showIdle{true};           ///< 常态（未 hover）时是否显示圆点
    double radius{3.5};            ///< 常态圆点半径
    double hoverRadius{5.5};       ///< hover 时圆点放大半径
    double haloRadius{10.0};       ///< hover 时外圈光晕半径（<= 0 不显示）
    float haloOpacity{0.22f};      ///< 外圈光晕透明度
    double hitRadius{10.0};        ///< 指针磁吸命中半径（像素）
    double strokeWidth{1.5};       ///< 圆点描边宽度
    Paint fill{};                  ///< 圆点填充色；不可见时由 LineChart 按 seriesIndex 赋予系列色
    Paint stroke{core::ColorToken::Background};  ///< 圆点外描边色
};

/// 折线图单个数据节点组件：继承 TooltipTarget，自己维护 Hover 状态与气泡。
class LinePointNode final : public TooltipTarget {
public:
    explicit LinePointNode(std::string key = "point");

    // ------------------------------------------------------------ 位置与数据（由父组件设置）

    /// 设置节点在父组件坐标系下的圆心像素位置。
    void setPointPosition(const Vec2d& center) noexcept;
    [[nodiscard]] Vec2d pointPosition() const noexcept { return pointPosition_; }

    /// 设置节点绑定的数据元信息（供默认 Tooltip 标题与正文生成使用）。
    void setPointData(std::size_t seriesIndex, std::size_t pointIndex, const Vec2d& dataValue,
                      std::string seriesName = {}, std::string xLabel = {},
                      std::string yLabel = {});

    [[nodiscard]] std::size_t seriesIndex() const noexcept { return seriesIndex_; }
    [[nodiscard]] std::size_t pointIndex() const noexcept { return pointIndex_; }
    [[nodiscard]] const Vec2d& dataValue() const noexcept { return dataValue_; }
    [[nodiscard]] const std::string& seriesName() const noexcept { return seriesName_; }
    [[nodiscard]] const std::string& xLabel() const noexcept { return xLabel_; }
    [[nodiscard]] const std::string& yLabel() const noexcept { return yLabel_; }

    // ------------------------------------------------------------ 样式

    void setPointStyle(const LinePointStyle& style);
    [[nodiscard]] const LinePointStyle& pointStyle() const noexcept { return style_; }

    // ------------------------------------------------------------ 光标与气泡（TooltipTarget）

    [[nodiscard]] core::CursorShape cursorAt(const Vec2d& worldPoint) const noexcept override;
    [[nodiscard]] TooltipText tooltipContent() const override;
    [[nodiscard]] bool hasTooltipContent() const override;
    [[nodiscard]] core::RectD tooltipAnchor() const override;

protected:
    [[nodiscard]] bool hitShape(const Vec2d& worldPoint) const noexcept override;
    void onPointerEnter() override;
    void onPointerLeave() override;
    void onUpdate(const FrameContext& ctx) override;

private:
    void syncVisual();
    [[nodiscard]] double maxInteractiveRadius() const noexcept;

    Vec2d pointPosition_{};
    std::size_t seriesIndex_{0};
    std::size_t pointIndex_{0};
    Vec2d dataValue_{};
    std::string seriesName_{};
    std::string xLabel_{};
    std::string yLabel_{};

    LinePointStyle style_{};
    bool visualDirty_{true};
    bool lastHovered_{false};

    core::SceneNode* halo_{nullptr};
    core::SceneNode* dot_{nullptr};
};

/// 折线图整体配置（双轴刻度 Text、文字样式/方向/角度、曲线类型、节点样式等）。
struct LineChartConfig {
    /// 坐标系与双轴布局选项（默认 yDirection = Up，下小上大）。
    /// x / y 均为 core::AxisOptions，可配置：
    ///   - tickLabels（X/Y 轴自定义刻度 Text）
    ///   - labelFontSize / labelColor / labelDirection（横排/竖排）/ labelAngle（偏移角度）
    ///   - title / titleFontSize / titleColor / titleDirection / titleAngle
    core::LayoutOptions layout{
        .padding = core::Padding::All(20.0),
        .yDirection = core::YDirection::Up,
    };

    core::LineCurve curve{core::LineCurve::Linear};
    double lineWidth{2.0};
    int curveSegments{16};

    bool showArea{false};
    double areaOpacity{0.16};

    bool showGrid{true};
    bool showCategoryGrid{false};
    bool showPlotBackground{true};
    bool showAxisLines{true};
    bool showAxisLabels{true};
    bool showAxisTitles{true};

    std::string title{};
    double titleFontSize{15.0};
    double titleGap{10.0};

    std::size_t secondarySeriesCount{0};

    /// 数据节点样式（传递给每个 LinePointNode 子组件）。
    LinePointStyle pointStyle{};

    /// 文本测量回调；为空时用 core::defaultTextMeasure 估算。
    core::TextMeasureFn textMeasure{};
};

/// 折线图组件：专注于双轴/网格/折线排版与数据映射，将数据节点拆分为 LinePointNode 子组件。
class LineChart final : public Widget {
public:
    explicit LineChart(std::string key = "line");

    // ------------------------------------------------------------ 数据

    void setData(core::Dataset data);
    void clearData();
    [[nodiscard]] const core::Dataset& data() const noexcept { return data_; }

    // ------------------------------------------------------------ 配置与样式

    void setConfig(const LineChartConfig& config);
    [[nodiscard]] const LineChartConfig& config() const noexcept { return config_; }

    void setXAxis(const core::AxisOptions& xAxis);
    [[nodiscard]] const core::AxisOptions& xAxis() const noexcept { return config_.layout.x; }

    void setYAxis(const core::AxisOptions& yAxis);
    [[nodiscard]] const core::AxisOptions& yAxis() const noexcept { return config_.layout.y; }

    void setPointStyle(const LinePointStyle& style);
    [[nodiscard]] const LinePointStyle& pointStyle() const noexcept { return config_.pointStyle; }

    void setPalette(std::vector<Color> palette);
    [[nodiscard]] const std::vector<Color>& palette() const noexcept { return palette_; }

    void setTextMeasure(core::TextMeasureFn measure);

    // ------------------------------------------------------------ 布局与节点查询

    [[nodiscard]] const core::ChartLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] bool valid() const noexcept { return valid_; }

    /// 当前挂载的全部数据节点子组件。
    [[nodiscard]] std::span<LinePointNode* const> pointNodes() const noexcept {
        return pointNodes_;
    }

    /// 当前处于 hover 状态的 LinePointNode（无则返回 nullptr）。
    [[nodiscard]] const LinePointNode* hoveredPointNode() const noexcept;

protected:
    void onUpdate(const FrameContext& ctx) override;

private:
    void rebuild();
    void syncPointNodes();
    [[nodiscard]] std::string resolveXTickText(std::size_t pointIndex, double xValue) const;
    [[nodiscard]] std::string resolveYTickText(double yValue, bool useSecondaryY) const;

    core::Dataset data_{};
    LineChartConfig config_{};
    bool dataDirty_{true};

    std::vector<Color> palette_{};
    std::shared_ptr<const std::vector<Color>> paletteShared_{};

    core::ChartLayout layout_{};
    core::Vec2d lastSize_{};
    bool valid_{false};

    core::SceneNode* sceneGroup_{nullptr};
    core::SceneNode* pointsGroup_{nullptr};
    std::vector<LinePointNode*> pointNodes_{};
};

}  // namespace kiriyama::scene
