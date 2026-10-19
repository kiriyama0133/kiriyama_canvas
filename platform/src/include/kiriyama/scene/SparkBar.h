#pragma once

// SparkStackBar 组件：**持有 model**（值语义）+ core::computeSparkBarLayout（几何）+ 样式 + 气泡。
//
// 继承 scene::TooltipTarget（和 PieChart 同一条路子）：
//   · 命中是精确的——按每根柱子的矩形判定（cover 到 tag 行），不是整块外接框；
//   · 气泡内容按"指针落在哪根柱 / 哪一段"动态生成：
//       落在某段上   →  "2024 · Cost" / "7.00 · 29.2%"（占整柱比例）
//       落在柱上     →  "2024" / "Revenue 19.00 · Cost 7.00 · 合计 26.00"
//   · 定制入口沿用 TooltipTarget 的约定：setTooltipContent() 设静态内容优先，
//     或继承覆盖 tooltipContent() 按业务算，或 TooltipHost::setResolver() 接管。
//
// 组件**持有 model**（setData 拷贝进来，值语义），只缓存派生的布局。
// frame / 数据 / options 变了才重算。不建逐柱子节点——柱很轻，一屏几十根
// 全建 Widget 反而更贵；命中与内容都从布局直接算，更新就是一次布局重算，
// 没有可 diff 的节点树（不存在"全量重建"问题）。

#include <cstdint>
#include <string>

#include "kiriyama/core/SparkBar.h"
#include "kiriyama/scene/TooltipTarget.h"

namespace kiriyama::scene {

class SparkBar final : public TooltipTarget {
public:
    explicit SparkBar(std::string key = "sparkbar");

    // ------------------------------------------------------------ 数据

    /// 拷贝进组件；之后调用方改自己的副本互不影响。
    void setData(const core::SparkBarData& data);
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
    /// 渲染层用它决定给哪一段加 hover 描边；tooltip 的 pick 与它同源。
    [[nodiscard]] std::size_t hoveredItem() const noexcept { return hoverItem_; }
    [[nodiscard]] std::size_t hoveredSegment() const noexcept { return hoverSegment_; }

    // ------------------------------------------------------------ 气泡（TooltipTarget）

    [[nodiscard]] TooltipText tooltipContent() const override;
    [[nodiscard]] bool hasTooltipContent() const override;
    /// 锚在当前指针命中的那根柱上——气泡跟着柱走，不贴整块画布。
    [[nodiscard]] core::RectD tooltipAnchor() const override;

protected:
    /// 按逐柱矩形判定命中（柱高 0 时退化成 tag 行）。
    [[nodiscard]] bool hitShape(const Vec2d& worldPoint) const noexcept override;

    /// frame / 数据 / options 变化时在这里重算（延迟到第一帧，
    /// 因为 setFrame 可能晚于 setData）。
    void onUpdate(const FrameContext& ctx) override;

private:
    void rebuild();
    void syncScene();
    void buildDescription(core::SceneNode& root) const;

    /// 指针命中的柱 / 段；没命中时下标为 core::kInvalidIndex。
    struct Pick {
        std::size_t item{core::kInvalidIndex};
        std::size_t segment{core::kInvalidIndex};
        std::size_t legendSeries{core::kInvalidIndex};
    };
    [[nodiscard]] Pick pickAtLocal(const Vec2d& local) const noexcept;

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
