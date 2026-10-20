#pragma once

// 可滚动容器几何与双向滚动数学（core 层，零 SDL 依赖）：
//   · 仿 CSS 的 Overflow 模式（Hidden / Auto / Scroll），独立控制 X 轴与 Y 轴；
//   · 计算视口裁剪框 viewport（支持悬浮式 overlayScrollbars 或占位挤压式滚动条）；
//   · 处理横纵两条滚动条同时出现时的右下角交汇避让（corner）；
//   · 提供像素偏移 scrollOffset 与归一化进度 progress ∈ [0, 1] 的双向映射、
//     滑块拖拽增量反算（scrollDeltaFromThumbDelta）及轨道点击跳转（scrollForTrackPosition）；
//   · 提供对任意 SceneNode 内容子树的逻辑尺寸自动测量 measureContentBounds()。

#include <cstdint>

#include "Geometry.h"
#include "SceneNode.h"

namespace kiriyama::core {

/// 仿 CSS overflow-x / overflow-y 的溢出控制模式。
enum class Overflow : std::uint8_t {
    Hidden,  ///< 裁剪溢出内容，禁止该轴滚动，且不显示该轴滚动条
    Auto,    ///< 仅当 contentSize > viewSize 时开启滚动并显示滚动条（默认）
    Scroll,  ///< 始终显示该轴滚动槽（即使内容未溢出，避免动态增减内容时视口跳动）
};

/// 滚动条几何度量参数（样式中的尺寸部分）。
struct ScrollBarMetrics {
    double thickness{8.0};        ///< 滚动条粗细（纵向为宽度，横向为高度）
    double margin{3.0};           ///< 滚动槽距容器外边缘及两端的内缩边距
    double minThumbLength{24.0};  ///< 滑块最小可抓取长度（防超长内容时滑块退化成细线）
    double cornerRadius{4.0};     ///< 滚动槽与滑块的胶囊圆角半径

    /// 非悬浮模式下单条滚动条占用的总通道厚度（thickness + 2 * margin）。
    [[nodiscard]] constexpr double channelSpan() const noexcept {
        return (thickness > 0.0 ? thickness : 0.0) + 2.0 * (margin > 0.0 ? margin : 0.0);
    }
};

/// 单个轴向（横向或纵向）的滚动几何计算结果。
struct ScrollAxisBar {
    bool visible{false};     ///< 是否绘制该轴滚动条（轨道 + 滑块）
    bool scrollable{false};  ///< 内容是否超出视口可滚动（maxScroll > 0）

    RectD track{};           ///< 滚动槽矩形（容器局部坐标）
    RectD thumb{};           ///< 滚动滑块矩形（容器局部坐标）

    double viewLength{0.0};     ///< 该轴可见视口长度
    double contentLength{0.0};  ///< 该轴完整内容长度
    double maxScroll{0.0};      ///< 最大可滚动像素偏移 max(0, contentLength - viewLength)
    double scroll{0.0};         ///< 已夹紧到 [0, maxScroll] 的当前滚动像素偏移
    double progress{0.0};       ///< 归一化滚动进度 scroll / maxScroll ∈ [0, 1]

    /// 将任意像素偏移夹紧到 [0, maxScroll]。
    [[nodiscard]] double clampScroll(double offset) const noexcept;

    /// 将归一化进度 t ∈ [0, 1] 换算为像素偏移。
    [[nodiscard]] double scrollForProgress(double normalizedProgress) const noexcept;

    /// 滑块拖拽反算：鼠标拖动滑块沿轴向移动 thumbDelta 像素时，对应的内容滚动像素增量。
    /// isVertical == true 时取 track.height - thumb.height，否则取 track.width - thumb.width。
    [[nodiscard]] double scrollDeltaFromThumbDelta(double thumbDelta,
                                                   bool isVertical) const noexcept;

    /// 轨道点击跳转：点击轨道上坐标 axisCoord（纵向传 y，横向传 x）时，
    /// 使滑块中心对齐到 axisCoord 所需的目标 scroll 像素偏移（已夹紧到 [0, maxScroll]）。
    [[nodiscard]] double scrollForTrackPosition(double axisCoord,
                                                bool isVertical) const noexcept;
};

/// 双轴滚动容器的完整布局结果。
struct ScrollLayout {
    RectD viewport{};        ///< 内容可见裁剪框 {0, 0, viewWidth, viewHeight}
    Vec2d contentSize{};     ///< 内容完整逻辑尺寸 {contentWidth, contentHeight}
    Vec2d maxScroll{};       ///< 双轴最大滚动偏移 {horizontal.maxScroll, vertical.maxScroll}
    Vec2d scroll{};          ///< 已夹紧的当前滚动偏移 {horizontal.scroll, vertical.scroll}
    Vec2d progress{};        ///< 双轴归一化进度 {horizontal.progress, vertical.progress} ∈ [0, 1]²

    ScrollAxisBar horizontal{};  ///< 底部横向滚动条（X 轴）
    ScrollAxisBar vertical{};    ///< 右侧纵向滚动条（Y 轴）
    RectD corner{};              ///< 横纵滚动条同时可见时的右下角交汇区

    [[nodiscard]] Vec2d clampScroll(Vec2d offset) const noexcept {
        return {horizontal.clampScroll(offset.x), vertical.clampScroll(offset.y)};
    }

    [[nodiscard]] Vec2d scrollForProgress(Vec2d normalizedProgress) const noexcept {
        return {horizontal.scrollForProgress(normalizedProgress.x),
                vertical.scrollForProgress(normalizedProgress.y)};
    }
};

/// 计算双轴滚动布局的输入参数。
struct ScrollLayoutInput {
    Vec2d containerSize{};               ///< ScrollWidget 容器整体宽高
    Vec2d contentSize{};                 ///< 子组件内容总逻辑宽高
    Vec2d scrollOffset{};                ///< 期望的滚动像素偏移 {scrollX, scrollY}
    Overflow overflowX{Overflow::Auto};  ///< 横向溢出策略
    Overflow overflowY{Overflow::Auto};  ///< 纵向溢出策略
    bool overlayScrollbars{true};        ///< true: 滚动条悬浮于内容之上；false: 预留通道挤压 viewport
    ScrollBarMetrics metrics{};          ///< 滚动条尺寸度量
};

/// 纯函数：根据容器尺寸、内容尺寸、溢出策略与当前偏移计算完整双轴滚动布局。
[[nodiscard]] ScrollLayout computeScrollLayout(const ScrollLayoutInput& input) noexcept;

/// 自动测量内容根节点下所有可见子节点的逻辑尺寸包络（max(right) + padding.x, max(bottom) + padding.y）。
/// 支持同时包含普通图元（Rect/Polygon/Text）与组件子树（Widget/Group）的内容容器。
[[nodiscard]] Vec2d measureContentBounds(const SceneNode& contentRoot,
                                         Vec2d padding = {}) noexcept;

}  // namespace kiriyama::core
