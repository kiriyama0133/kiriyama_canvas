#pragma once

// 饼图组件：数据指针 + core::computePieLayout（几何）+ 一组 PieSlice（交互）。
//
// 分工（和 Table 同一条约定——组件不持有数据）：
//   core::PieChartData   归调用方，这里只留 const 指针；
//   core::PieLayout      纯派生几何，frame / options / version 任一变化就重算；
//   scene::PieSlice      每个可用扇区一个子节点，命中 / hover / 气泡都在片上。
//
// 0 值、负值、NaN 片不建节点（computePieLayout 本来就会跳过），
// 所以 sliceCount 是"可用扇区数"，不是 data.slices.size()。
//
// 扇区坐标：PieSlice 的 arc 存**父坐标系**坐标（圆心 = frame 左上角 + 布局圆心），
// 自身 transform 保持单位。渲染层在 PieChart 的父坐标系里画即可。
//
// 用法：
//   scene::PieChart pie("pie");
//   pie.setFrame({48, 92, 864, 380});
//   pie.setData(&data, version);
//   // 每帧
//   pie.tickTree(render::frameContext(app));
//   renderer.draw(pie);                        // render::PieChartRenderer
//   tooltip.update(pie, render::frameContext(app));  // 片是 TooltipTarget，自动弹

#include <cstdint>
#include <vector>

#include "kiriyama/core/Data.h"
#include "kiriyama/core/Layout.h"
#include "kiriyama/scene/PieSlice.h"

namespace kiriyama::scene {

class PieChart final : public Widget {
public:
    explicit PieChart(std::string key = "pie");

    // ------------------------------------------------------------ 数据

    /// data 归调用方，必须活得比本组件久。version 变了才重建扇区节点。
    /// 传 nullptr = 清空。
    void setData(const core::PieChartData* data, std::uint64_t version = 0);
    [[nodiscard]] const core::PieChartData* data() const noexcept { return data_; }

    // ------------------------------------------------------------ 布局

    void setLayoutOptions(const core::PieLayoutOptions& options);
    [[nodiscard]] const core::PieLayoutOptions& layoutOptions() const noexcept { return options_; }

    [[nodiscard]] const core::PieLayout& layout() const noexcept { return layout_; }
    /// 有可画的扇区时为 true。
    [[nodiscard]] bool valid() const noexcept { return layout_.valid && sliceCount() > 0; }

    // ------------------------------------------------------------ 外观

    /// 扇区填色，按序循环取用。为空时用 core::defaultSeriesPalette()。
    void setPalette(std::vector<Color> palette);
    [[nodiscard]] const std::vector<Color>& palette() const noexcept { return palette_; }

    /// 整圆细分数，扇区按弧长比例取用（钳到 12..512）。默认 96。
    void setSegments(int segments) noexcept { segments_ = segments; }
    [[nodiscard]] int segments() const noexcept { return segments_; }

    /// 扇区默认外观（hover 外扩距离），rebuild 时应用到每个扇区。
    /// 每片的单独微调拿到 sliceAt(i) 再改。
    void setSliceDefaults(double explodeDistance) noexcept {
        explodeDistance_ = explodeDistance;
        dataDirty_ = true;
    }

    // ------------------------------------------------------------ 查询

    /// 可用扇区数（0 值 / 负值 / NaN 片不算）。
    [[nodiscard]] std::size_t sliceCount() const noexcept;
    [[nodiscard]] PieSlice* sliceAt(std::size_t index) noexcept;
    [[nodiscard]] const PieSlice* sliceAt(std::size_t index) const noexcept;
    /// 当前悬停的扇区；没有则 nullptr。要求先 tickTree。
    [[nodiscard]] PieSlice* hoveredSlice() const noexcept;

protected:
    /// version / frame 变化时在这里重建（构造顺序上 setFrame 可能晚于 setData，
    /// 延迟到第一帧，布局总是拿到确定的 frame）。
    void onUpdate(const FrameContext& ctx) override;

private:
    void rebuild();

    const core::PieChartData* data_{nullptr};
    std::uint64_t version_{0};
    bool dataDirty_{true};

    core::PieLayoutOptions options_{};
    std::vector<Color> palette_{};
    int segments_{96};

    double explodeDistance_{6.0};

    core::PieLayout layout_{};
    core::RectD lastFrame_{};
};

}  // namespace kiriyama::scene
