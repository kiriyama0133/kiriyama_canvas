#pragma once

// 饼图组件：**组件拥有 model**（值语义），更新走"描述树 → reconcileScene"。
//
// 数据流（和 Vue 的 patch 同构）：
//   model（组件持有，setData 拷贝进来）
//     → buildPieChart() 生成描述树（core，只描述不持有资源）
//     → reconcileScene(plot 组, 描述树) 就地对账：key 相同的节点保身份、
//       只写变化的属性；数据热更新不再 clearChildren 全量重建，
//       hover / 外扩动画不闪断。
//
// 交互状态全部在**图表级**，不在扇区节点上（扇区只是被 diff 维护的图元）：
//   · 命中：core::hitTestPie（半径 + 角度，甜甜圈内孔不算）；
//   · hover：hoveredIndex_；
//   · 外扩：explodeProgress_ 逐扇区动画，动画只对变化中的扇区 setPoints
//     （原地更新、标脏），不重建节点。
//
// 气泡：图表自身就是 TooltipTarget——锚在整张 frame（hover 哪片都浮在图上方），
// 内容按 hoveredIndex 生成"数值 · 占比%"；静态内容优先（TooltipTarget 入口一）。
//
// 0 值、负值、NaN 片不产生扇区（computePieLayout 跳过），
// sliceCount 是"可用扇区数"，不是 data.slices.size()。

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

class PieChart final : public TooltipTarget {
public:
    explicit PieChart(std::string key = "pie");

    // ------------------------------------------------------------ 数据（组件持有）

    /// 拷贝进组件；之后调用方改自己的副本互不影响。
    void setData(const core::PieChartData& data);
    void clearData();
    [[nodiscard]] const core::PieChartData& data() const noexcept { return data_; }

    // ------------------------------------------------------------ 布局

    void setLayoutOptions(const core::PieLayoutOptions& options);
    [[nodiscard]] const core::PieLayoutOptions& layoutOptions() const noexcept { return options_; }

    [[nodiscard]] const core::PieLayout& layout() const noexcept { return layout_; }
    /// 有可画的扇区时为 true。
    [[nodiscard]] bool valid() const noexcept;

    // ------------------------------------------------------------ 外观

    /// 扇区填色，按序循环取用。为空时用 core::defaultSeriesPalette()。
    void setPalette(std::vector<Color> palette);
    [[nodiscard]] const std::vector<Color>& palette() const noexcept { return palette_; }

    /// 整圆细分数，扇区按弧长比例取用（钳到 12..512）。默认 96。
    void setSegments(int segments) noexcept;

    /// hover 外扩距离（像素）。
    void setSliceDefaults(double explodeDistance) noexcept;

    /// 外侧标签（原 render::PieStyle 的字段，样式协议直接进组件）。
    void setLabelStyle(bool showLabels, bool showValues, double fontSize, Color color);
    /// 扇区缝隙描边。默认关（width 0）——hover 外扩时缝隙线会扫出残影。
    void setGapStyle(double width, Color color);

    /// 描述树文本定位的测量回调；空 = core 估算。宿主接真实字体度量可消除估宽误差。
    void setTextMeasure(core::TextMeasureFn measure) { textMeasure_ = std::move(measure); }

    // ------------------------------------------------------------ 查询

    /// 可用扇区数（0 值 / 负值 / NaN 片不算）。
    [[nodiscard]] std::size_t sliceCount() const noexcept { return layout_.sectors.size(); }
    /// 当前悬停的扇区下标；core::kInvalidIndex = 没有。要求先 tickTree。
    [[nodiscard]] std::size_t hoveredIndex() const noexcept { return hoveredIndex_; }
    /// 扇区当前的半径增量（外扩动画进度 × 距离）。
    [[nodiscard]] double explodeRadius(std::size_t index) const noexcept;

    // ------------------------------------------------------------ 气泡（TooltipTarget）

    [[nodiscard]] TooltipText tooltipContent() const override;
    [[nodiscard]] bool hasTooltipContent() const override;
    /// 锚在整张图表 frame 上（与旧 PieSlice 的约定一致）。
    [[nodiscard]] core::RectD tooltipAnchor() const override;

protected:
    /// 命中 = 指针落在某个扇区上（外扩中的扇区按放大后的外半径判定）。
    [[nodiscard]] bool hitShape(const Vec2d& worldPoint) const noexcept override;

    void onUpdate(const FrameContext& ctx) override;

private:
    void rebuild();
    void updateHoverAndExplode(const FrameContext& ctx);
    /// 把扇区 i 的多边形写成"外半径 + radius"的版本（radius 0 = 基准几何）。
    void applyExplode(std::size_t index, double radius);
    [[nodiscard]] core::SceneBuildOptions buildOptions() const;
    void collectSliceNodes();

    core::PieChartData data_{};
    bool dataDirty_{true};

    core::PieLayoutOptions options_{};
    std::vector<Color> palette_{};
    /// palette_ 的共享只读版本，挂到对账根组的 style.seriesPalette 上。
    /// 缓住指针：palette 没变时 rebuild 不产生新的样式差异（diff 零写入）。
    std::shared_ptr<const std::vector<Color>> paletteShared_{};
    int segments_{96};
    double explodeDistance_{6.0};

    bool showLabels_{true};
    bool showValues_{true};
    double labelFontSize_{12.5};
    Color labelColor_{core::Color::Hex(0x5A5A54)};
    double gapWidth_{0.0};
    Color gapColor_{core::Color::Hex(0xFFFFFF)};
    core::TextMeasureFn textMeasure_{};

    core::PieLayout layout_{};
    core::RectD lastFrame_{};

    /// 两层结构：plot 组持 frame 平移（组件自己写），inner 组是对账目标
    /// （reconcile 会同步目标根的 key / transform，所以平移必须放在它之上）。
    core::SceneNode* plot_{nullptr};
    core::SceneNode* inner_{nullptr};
    /// 对账后收集的扇区节点（key slice.<i>），外扩动画原地改它们的 points。
    std::vector<core::SceneNode*> sliceNodes_{};

    std::size_t hoveredIndex_{core::kInvalidIndex};
    std::vector<double> explodeProgress_{};
};

}  // namespace kiriyama::scene
