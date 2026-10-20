#pragma once

// 饼图交互组件（scene 层，对齐 SparkBar / TableWidget）：
//   · 组件自身持有 PieChartData（值语义）与 core::PieStyle；
//   · 纯几何布局计算（computePieLayout）、描述树构建（buildPieSlices）与
//     扇区外扩局部更新（updatePieSliceExplode）全部由 core::SceneBuilder 提供；
//   · 本组件只管：接收指针命中、驱动外扩动画进度、生成 Tooltip 文本，
//     并在数据/尺寸变化时调 buildPieSlices + reconcileScene，
//     在外扩动画帧调 updatePieSliceExplode 就地 O(1) 更新顶点。

#include <cstdint>
#include <memory>
#include <vector>

#include "kiriyama/core/Data.h"
#include "kiriyama/core/HitTest.h"
#include "kiriyama/core/Layout.h"
#include "kiriyama/core/SceneBuilder.h"
#include "kiriyama/scene/TooltipTarget.h"

namespace kiriyama::scene {

using core::Color;
using PieStyle = core::PieStyle;

class PieChart final : public TooltipTarget {
public:
    explicit PieChart(std::string key = "pie");

    // ------------------------------------------------------------ 数据（组件持有）

    void setData(const core::PieChartData& data);
    void clearData();
    [[nodiscard]] const core::PieChartData& data() const noexcept { return data_; }

    // ------------------------------------------------------------ 样式与布局

    void setStyle(const PieStyle& style);
    [[nodiscard]] const PieStyle& style() const noexcept { return style_; }

    void setLayoutOptions(const core::PieLayoutOptions& options);
    [[nodiscard]] const core::PieLayoutOptions& layoutOptions() const noexcept {
        return style_.layout;
    }

    [[nodiscard]] const core::PieLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] bool valid() const noexcept;

    /// 扇区填色，按序循环取用。为空时用 core::defaultSeriesPalette()。
    void setPalette(std::vector<Color> palette);
    [[nodiscard]] const std::vector<Color>& palette() const noexcept { return palette_; }

    /// 整圆细分数，扇区按弧长比例取用（钳到 12..512）。默认 96。
    void setSegments(int segments) noexcept;

    /// hover 外扩距离（像素）。
    void setSliceDefaults(double explodeDistance) noexcept;

    /// 外侧标签样式。
    void setLabelStyle(bool showLabels, bool showValues, double fontSize, Color color);
    /// 扇区缝隙描边。
    void setGapStyle(double width, Color color);

    /// 描述树文本定位的测量回调；空 = core 估算。
    void setTextMeasure(core::TextMeasureFn measure) {
        style_.textMeasure = std::move(measure);
        dataDirty_ = true;
    }

    // ------------------------------------------------------------ 查询与光标

    [[nodiscard]] std::size_t sliceCount() const noexcept { return layout_.sectors.size(); }
    [[nodiscard]] std::size_t hoveredIndex() const noexcept { return hoveredIndex_; }
    [[nodiscard]] double explodeRadius(std::size_t index) const noexcept;
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
    void updateHoverAndExplode(const FrameContext& ctx);
    [[nodiscard]] std::size_t pickSector(const Vec2d& worldPoint) const noexcept;

    core::PieChartData data_{};
    PieStyle style_{};
    bool dataDirty_{true};

    std::vector<Color> palette_{};
    std::shared_ptr<const std::vector<Color>> paletteShared_{};

    core::PieLayout layout_{};
    core::Vec2d lastSize_{};
    core::SceneNode* inner_{nullptr};

    std::size_t hoveredIndex_{core::kInvalidIndex};
    std::vector<double> explodeProgress_{};
};

}  // namespace kiriyama::scene
