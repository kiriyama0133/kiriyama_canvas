#pragma once

// SparkStackBar 交互组件（scene 层，对齐 PieChart）：
//   · 组件自身持有 SparkBarData（值语义）与交互状态（hover 项 / 段 / 图例）；
//   · 纯几何布局计算、描述树构建（buildSparkBar）、命中判定（hitTestSparkBar）全部在 core；
//   · 本组件只管：指针命中更新、Tooltip 文本生成、并在数据/尺寸变化时调 core::buildSparkBar + reconcileScene，
//     在单纯 hover 变化时调 core::updateSparkBarHover 就地 O(1) 更新。

#include <cstdint>
#include <string>
#include <vector>

#include "kiriyama/core/SparkBar.h"
#include "kiriyama/scene/TooltipTarget.h"

namespace kiriyama::scene {

class SparkBar final : public TooltipTarget {
public:
    explicit SparkBar(std::string key = "sparkbar");

    // ------------------------------------------------------------ 数据

    /// 组件自己持有数据（值语义 / 移动语义接管）。
    void setData(core::SparkBarData data);
    [[nodiscard]] const core::SparkBarData& data() const noexcept { return data_; }

    // ------------------------------------------------------------ 样式

    void setOptions(const core::SparkBarOptions& options);
    [[nodiscard]] const core::SparkBarOptions& options() const noexcept { return options_; }

    /// 堆叠层配色（按 series 顺序循环）。为空时用 core::defaultSeriesPalette()。
    void setColors(std::vector<core::Color> colors);
    [[nodiscard]] const std::vector<core::Color>& colors() const noexcept { return colors_; }
    [[nodiscard]] core::Color colorAt(std::size_t seriesIndex) const noexcept;

    [[nodiscard]] const core::SparkBarLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] bool valid() const noexcept { return layout_.valid; }

    /// 本帧指针命中的柱 / 段（没命中为 core::kInvalidIndex）。
    [[nodiscard]] std::size_t hoveredItem() const noexcept { return hoverItem_; }
    [[nodiscard]] std::size_t hoveredSegment() const noexcept { return hoverSegment_; }
    [[nodiscard]] core::CursorShape cursorAt(const Vec2d& worldPoint) const noexcept override;

    // ------------------------------------------------------------ 气泡（TooltipTarget）

    [[nodiscard]] TooltipText tooltipContent() const override;
    [[nodiscard]] bool hasTooltipContent() const override;
    [[nodiscard]] core::RectD tooltipAnchor() const override;

protected:
    [[nodiscard]] bool hitShape(const Vec2d& worldPoint) const noexcept override;
    void onUpdate(const FrameContext& ctx) override;

private:
    void rebuild();
    void syncHover();

    core::SparkBarData data_{};
    bool dataDirty_{true};

    core::SparkBarOptions options_{};
    std::vector<core::Color> colors_{};

    core::SparkBarLayout layout_{};
    core::Vec2d lastSize_{};
    core::SceneNode* inner_{nullptr};

    std::size_t hoverItem_{core::kInvalidIndex};
    std::size_t hoverSegment_{core::kInvalidIndex};
    std::size_t hoverLegendSeries_{core::kInvalidIndex};

    std::size_t lastHoverItem_{core::kInvalidIndex};
    std::size_t lastHoverSegment_{core::kInvalidIndex};
};

}  // namespace kiriyama::scene
