#pragma once

// 虚拟滚动：长列表只渲染视口内的项，绘制量与总条数无关。
//
// 这一层只做数学，不持有数据、不碰绘制，负责回答四件事：
//   1. 该渲染哪几项     renderRange() / visibleRange()
//   2. 每一项在哪       itemRect() / itemTop()
//   3. 点在哪个项上     indexAtViewportY() / indexAtContentY()
//   4. 选中项是谁       selectedIndex()，与渲染范围解耦，滚出屏幕也不丢
//
// 行高固定（均匀行高）。需要"总高度"而不是行高时用 FromTotalHeight() 或
// setTotalHeight()，行高由总数均分。非均匀行高请在数据层自行摊平成像素表，
// 这个组件刻意不做前缀和缓存——那条路的复杂度和收益不成正比。
//
// 滚动物理（惯性、回弹、弹簧）由调用方负责：每帧把新 offset 交给 setScrollOffset()，
// 组件只做夹取，不做插值。

#include <cstddef>

#include "Geometry.h"

namespace kiriyama::core {

/// 无选中项 / 坐标不在任何项上。
inline constexpr int kNoItem = -1;

/// scrollToItem 的对齐方式。目标位置会自动夹进 [0, maxScrollOffset]，
/// 因此靠近两端的 Center / End 会自然退化成 Start / End 对齐。
enum class ScrollAlign {
    Nearest,  ///< 已经完整可见就不动，否则最小距离滚动
    Start,    ///< 项顶对齐视口顶
    Center,   ///< 项中心对齐视口中心
    End,      ///< 项底对齐视口底
};

struct VirtualScrollOptions {
    /// 视口上方 / 下方各多渲染几项。快速滚动时靠它兜住白边，正常给 2~5。
    int bufferItems{3};
    /// 额外的像素级缓冲，按行高换算成项数累加。高 DPI 或高速惯性滚动时给几十像素。
    double bufferPixels{0.0};
};

/// 渲染范围，半开区间 [first, end())。
/// 可以直接 for 循环：`for (int i = range.first; i < range.end(); ++i)`。
struct ItemRange {
    int first{0};
    int count{0};

    constexpr ItemRange() noexcept = default;
    constexpr ItemRange(int firstIndex, int itemCount) noexcept
        : first(firstIndex), count(itemCount) {}

    [[nodiscard]] constexpr int end() const noexcept { return first + count; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count <= 0; }
    [[nodiscard]] constexpr bool contains(int index) const noexcept {
        return index >= first && index < end();
    }

    [[nodiscard]] constexpr bool operator==(const ItemRange&) const noexcept = default;
};

class VirtualScroll {
public:
    VirtualScroll() noexcept = default;

    /// itemHeight 非正或 viewportHeight 为负时，组件退化为不可滚动（范围为空），不抛错——
    /// 布局还没算出来的第一帧经常就是这样。
    VirtualScroll(int itemCount, double itemHeight, double viewportHeight,
                  const VirtualScrollOptions& options = {}) noexcept;

    /// 按"总高度"构造：行高 = totalHeight / itemCount。
    [[nodiscard]] static VirtualScroll FromTotalHeight(
        int itemCount, double totalHeight, double viewportHeight,
        const VirtualScrollOptions& options = {}) noexcept;

    // ------------------------------------------------------------ 参数

    /// 总条数变化。已有滚动位置保持不变（除非被新的 maxScrollOffset 夹取）；
    /// 想"追加数据后仍贴底"就再调一次 scrollToEnd()。
    void setItemCount(int count);

    /// 行高。非正 / 非有限值会被忽略，保留原行高。
    void setItemHeight(double height);

    /// 内容总高度，行高按当前条数均分。条数为 0 或高度非正时忽略。
    void setTotalHeight(double height);

    /// 视口高度变化后偏移量保持不变，只在新上界处夹取。
    /// 滚到底时把视口拉大仍然贴底（maxScrollOffset 变小，偏移被夹到新上界）。
    void setViewportHeight(double height);

    void setOptions(const VirtualScrollOptions& options) noexcept;

    // ------------------------------------------------------------ 状态查询

    [[nodiscard]] int itemCount() const noexcept { return itemCount_; }
    [[nodiscard]] double itemHeight() const noexcept { return itemHeight_; }
    [[nodiscard]] double viewportHeight() const noexcept { return viewportHeight_; }
    [[nodiscard]] double totalHeight() const noexcept { return totalHeight_; }
    [[nodiscard]] double scrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] double maxScrollOffset() const noexcept;
    [[nodiscard]] bool scrollable() const noexcept { return maxScrollOffset() > 0.0; }

    /// 滚动进度 0~1，给滚动条 / 进度指示用。不可滚动时为 0。
    [[nodiscard]] double scrollProgress() const noexcept;

    // ------------------------------------------------------------ 滚动

    /// 写偏移量，自动夹进 [0, maxScrollOffset]；NaN 当 0，±inf 按边界夹取。
    /// 返回是否真的变了——可以直接拿来做"要不要重绘"的判断。
    bool setScrollOffset(double offset);
    bool scrollBy(double delta);
    bool scrollToStart();
    bool scrollToEnd();

    /// 滚动到指定项。索引越界返回 false（不做夹取，避免误调用时静默滚到别处）。
    bool scrollToItem(int index, ScrollAlign align = ScrollAlign::Nearest);
    /// 已可见就不动，否则最小距离滚动。
    bool ensureItemVisible(int index, ScrollAlign align = ScrollAlign::Nearest);

    // ------------------------------------------------------------ 几何

    /// 项顶在内容坐标系中的 y（内容坐标原点 = 列表顶）。
    [[nodiscard]] double itemTop(int index) const noexcept;
    [[nodiscard]] double itemBottom(int index) const noexcept;

    /// 项在视口坐标系中的矩形（未裁剪，超出视口的部分由渲染层裁剪）。
    /// x / width 取自 viewport；索引非法或行高非正时返回空矩形。
    [[nodiscard]] RectD itemRect(int index, const RectD& viewport) const noexcept;

    /// 内容坐标反查索引。落在 [0, totalHeight) 之外返回 kNoItem。
    [[nodiscard]] int indexAtContentY(double y) const noexcept;
    /// 视口内坐标（0 = 视口顶）反查索引。允许传视口外的坐标，越界一样返回 kNoItem。
    [[nodiscard]] int indexAtViewportY(double yInViewport) const noexcept;

    // ------------------------------------------------------------ 渲染范围

    /// 与视口有交集的项（含被裁掉一半的首尾项），不含缓冲。
    [[nodiscard]] ItemRange visibleRange() const noexcept;
    /// 视口项 + 上下缓冲，渲染层按这个范围取数据。
    [[nodiscard]] ItemRange renderRange() const noexcept;
    /// 指定偏移下的渲染范围（含缓冲），用于预判"下一帧要哪些数据"。
    [[nodiscard]] ItemRange rangeForOffset(double offset) const noexcept;

    // ------------------------------------------------------------ 选中项

    /// -1 清除选中；超上界夹到末项。选中项与渲染范围无关，滚出屏幕仍然保持。
    void setSelectedIndex(int index) noexcept;
    [[nodiscard]] int selectedIndex() const noexcept { return selectedIndex_; }
    [[nodiscard]] bool hasSelection() const noexcept { return selectedIndex_ != kNoItem; }
    [[nodiscard]] bool isSelected(int index) const noexcept { return index == selectedIndex_; }
    /// 选中项是否落在渲染范围内（决定要不要真的画高亮）。
    [[nodiscard]] bool isSelectedVisible() const noexcept;

    /// 键盘上下键移动选中项。没有选中时，下移从首项开始、上移从末项开始。
    /// keepVisible 为 true 时顺带滚进视口。
    bool moveSelection(int delta, bool keepVisible = true);
    bool ensureSelectedVisible(ScrollAlign align = ScrollAlign::Nearest);

    // ------------------------------------------------------------ 滚动条

    /// 轨道内滑块矩形（纵向）。内容不足一屏时返回整条轨道。
    /// minThumbSize 是滑块最小高度，避免长列表里滑块退化成一条线点不中。
    [[nodiscard]] RectD thumbRect(const RectD& track, double minThumbSize = 24.0) const noexcept;

private:
    [[nodiscard]] ItemRange computeRange(double offset, int bufferItems,
                                         double bufferPixels) const noexcept;

    VirtualScrollOptions options_{};
    int itemCount_{0};
    double itemHeight_{0.0};
    double totalHeight_{0.0};
    double viewportHeight_{0.0};
    double scrollOffset_{0.0};
    int selectedIndex_{kNoItem};
};

}  // namespace kiriyama::core
