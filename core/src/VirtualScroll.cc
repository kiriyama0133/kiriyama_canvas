#include "kiriyama/core/VirtualScroll.h"

#include <algorithm>
#include <cmath>

namespace kiriyama::core {
namespace {

/// 尺寸类参数：负数、NaN、inf 全部归零。
[[nodiscard]] double sanitizeExtent(double value) noexcept {
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

/// 行高必须严格为正，否则整个几何都没有意义。
[[nodiscard]] double sanitizeItemHeight(double value) noexcept {
    if (!std::isfinite(value) || value <= 0.0) {
        return 0.0;
    }
    return value;
}

}  // namespace

VirtualScroll::VirtualScroll(int itemCount, double itemHeight, double viewportHeight,
                             const VirtualScrollOptions& options) noexcept
    : options_(options),
      itemCount_(itemCount > 0 ? itemCount : 0),
      itemHeight_(sanitizeItemHeight(itemHeight)),
      viewportHeight_(sanitizeExtent(viewportHeight)) {
    totalHeight_ = itemHeight_ * static_cast<double>(itemCount_);
}

VirtualScroll VirtualScroll::FromTotalHeight(int itemCount, double totalHeight,
                                             double viewportHeight,
                                             const VirtualScrollOptions& options) noexcept {
    VirtualScroll list(itemCount, 1.0, viewportHeight, options);
    list.setTotalHeight(totalHeight);
    return list;
}

// ---------------------------------------------------------------- 参数

void VirtualScroll::setItemCount(int count) {
    const int next = count > 0 ? count : 0;
    if (next == itemCount_) {
        return;
    }
    itemCount_ = next;
    totalHeight_ = itemHeight_ * static_cast<double>(itemCount_);
    if (selectedIndex_ >= itemCount_) {
        selectedIndex_ = kNoItem;
    }
    setScrollOffset(scrollOffset_);  // 夹到新的上界
}

void VirtualScroll::setItemHeight(double height) {
    const double next = sanitizeItemHeight(height);
    if (!(next > 0.0)) {
        return;
    }
    itemHeight_ = next;
    totalHeight_ = itemHeight_ * static_cast<double>(itemCount_);
    setScrollOffset(scrollOffset_);
}

void VirtualScroll::setTotalHeight(double height) {
    const double next = sanitizeExtent(height);
    if (itemCount_ <= 0 || !(next > 0.0)) {
        return;
    }
    itemHeight_ = next / static_cast<double>(itemCount_);
    totalHeight_ = next;
    setScrollOffset(scrollOffset_);
}

void VirtualScroll::setViewportHeight(double height) {
    // 行高固定时"顶部锚点"其实就是原偏移量本身，因此不需要额外保存：
    // 视口变大只会让 maxScrollOffset 变小，夹取后自然贴底（滚到底时行为正确）。
    viewportHeight_ = sanitizeExtent(height);
    setScrollOffset(scrollOffset_);
}

void VirtualScroll::setOptions(const VirtualScrollOptions& options) noexcept { options_ = options; }

// ---------------------------------------------------------------- 状态

double VirtualScroll::maxScrollOffset() const noexcept {
    const double maxOffset = totalHeight_ - viewportHeight_;
    return maxOffset > 0.0 ? maxOffset : 0.0;
}

double VirtualScroll::scrollProgress() const noexcept {
    const double maxOffset = maxScrollOffset();
    if (!(maxOffset > 0.0)) {
        return 0.0;
    }
    return math::clamp(scrollOffset_ / maxOffset, 0.0, 1.0);
}

// ---------------------------------------------------------------- 滚动

bool VirtualScroll::setScrollOffset(double offset) {
    const double maxOffset = maxScrollOffset();
    double next = offset;
    if (std::isnan(next)) {
        next = 0.0;
    }
    // ±inf 走正常的夹取路径：+inf -> 上界，-inf -> 0。
    if (next < 0.0) {
        next = 0.0;
    } else if (next > maxOffset) {
        next = maxOffset;
    }
    if (next == scrollOffset_) {
        return false;
    }
    scrollOffset_ = next;
    return true;
}

bool VirtualScroll::scrollBy(double delta) { return setScrollOffset(scrollOffset_ + delta); }

bool VirtualScroll::scrollToStart() { return setScrollOffset(0.0); }

bool VirtualScroll::scrollToEnd() { return setScrollOffset(maxScrollOffset()); }

bool VirtualScroll::scrollToItem(int index, ScrollAlign align) {
    if (index < 0 || index >= itemCount_) {
        return false;
    }

    const double top = itemTop(index);
    const double bottom = top + itemHeight_;
    constexpr double kEpsilon = 1e-9;

    double target = scrollOffset_;
    switch (align) {
        case ScrollAlign::Nearest: {
            const bool fullyVisible = top >= scrollOffset_ - kEpsilon &&
                                      bottom <= scrollOffset_ + viewportHeight_ + kEpsilon;
            if (fullyVisible) {
                return false;
            }
            target = top < scrollOffset_ ? top : bottom - viewportHeight_;
            break;
        }
        case ScrollAlign::Start:
            target = top;
            break;
        case ScrollAlign::Center:
            target = top + (itemHeight_ - viewportHeight_) * 0.5;
            break;
        case ScrollAlign::End:
            target = bottom - viewportHeight_;
            break;
    }
    // 三个对齐分支允许目标落在 [0, maxScrollOffset] 之外，setScrollOffset 会夹回来。
    return setScrollOffset(target);
}

bool VirtualScroll::ensureItemVisible(int index, ScrollAlign align) {
    return scrollToItem(index, align);
}

// ---------------------------------------------------------------- 几何

double VirtualScroll::itemTop(int index) const noexcept {
    if (itemCount_ <= 0 || !(itemHeight_ > 0.0)) {
        return 0.0;
    }
    if (index <= 0) {
        return 0.0;
    }
    if (index >= itemCount_) {
        return totalHeight_;
    }
    return static_cast<double>(index) * itemHeight_;
}

double VirtualScroll::itemBottom(int index) const noexcept {
    if (index < 0 || index >= itemCount_) {
        return itemTop(index);
    }
    return itemTop(index) + itemHeight_;
}

RectD VirtualScroll::itemRect(int index, const RectD& viewport) const noexcept {
    if (index < 0 || index >= itemCount_ || !(itemHeight_ > 0.0)) {
        return {};
    }
    return {viewport.x, viewport.y + itemTop(index) - scrollOffset_, viewport.width, itemHeight_};
}

int VirtualScroll::indexAtContentY(double y) const noexcept {
    if (itemCount_ <= 0 || !(itemHeight_ > 0.0) || !std::isfinite(y)) {
        return kNoItem;
    }
    if (y < 0.0 || y >= totalHeight_) {
        return kNoItem;
    }

    const double raw = std::floor(y / itemHeight_);
    if (raw < 0.0) {
        return 0;  // 抵消浮点误差：y 极小正数时 raw 可能仍为 0
    }
    if (raw >= static_cast<double>(itemCount_)) {
        return itemCount_ - 1;
    }
    return static_cast<int>(raw);
}

int VirtualScroll::indexAtViewportY(double yInViewport) const noexcept {
    if (!std::isfinite(yInViewport)) {
        return kNoItem;
    }
    return indexAtContentY(yInViewport + scrollOffset_);
}

// ---------------------------------------------------------------- 范围

ItemRange VirtualScroll::computeRange(double offset, int bufferItems,
                                      double bufferPixels) const noexcept {
    if (itemCount_ <= 0 || !(itemHeight_ > 0.0) || !(viewportHeight_ > 0.0)) {
        return {};
    }

    double top = offset;
    if (!std::isfinite(top)) {
        top = 0.0;
    }
    top = math::clamp(top, 0.0, maxScrollOffset());
    const double bottom = top + viewportHeight_;

    // 首项向下取整（顶部被裁掉一半的项也要画），
    // 末项向上取整并排他（正好贴底的项不算可见）。
    double rawFirst = std::floor(top / itemHeight_);
    double rawLast = std::ceil(bottom / itemHeight_);

    const double maxIndex = static_cast<double>(itemCount_) - 1.0;
    if (rawFirst < 0.0) {
        rawFirst = 0.0;
    } else if (rawFirst > maxIndex) {
        rawFirst = maxIndex;
    }
    if (rawLast < rawFirst + 1.0) {
        rawLast = rawFirst + 1.0;
    } else if (rawLast > static_cast<double>(itemCount_)) {
        rawLast = static_cast<double>(itemCount_);
    }

    int first = static_cast<int>(rawFirst);
    int last = static_cast<int>(rawLast);

    int buffer = bufferItems > 0 ? bufferItems : 0;
    if (std::isfinite(bufferPixels) && bufferPixels > 0.0) {
        const double extra = std::ceil(bufferPixels / itemHeight_);
        // 极端小行高 + 大像素缓冲时，换算结果可能超出 int 范围。
        if (extra < maxIndex + 1.0) {
            buffer += static_cast<int>(extra);
        } else {
            buffer = itemCount_;
        }
    }

    first = std::max(0, first - buffer);
    last = std::min(itemCount_, last + buffer);
    return {first, last - first};
}

ItemRange VirtualScroll::visibleRange() const noexcept {
    return computeRange(scrollOffset_, 0, 0.0);
}

ItemRange VirtualScroll::renderRange() const noexcept {
    return computeRange(scrollOffset_, options_.bufferItems, options_.bufferPixels);
}

ItemRange VirtualScroll::rangeForOffset(double offset) const noexcept {
    return computeRange(offset, options_.bufferItems, options_.bufferPixels);
}

// ---------------------------------------------------------------- 选中

void VirtualScroll::setSelectedIndex(int index) noexcept {
    if (index < 0 || itemCount_ <= 0) {
        selectedIndex_ = kNoItem;
        return;
    }
    selectedIndex_ = index >= itemCount_ ? itemCount_ - 1 : index;
}

bool VirtualScroll::isSelectedVisible() const noexcept {
    return selectedIndex_ != kNoItem && renderRange().contains(selectedIndex_);
}

bool VirtualScroll::moveSelection(int delta, bool keepVisible) {
    if (itemCount_ <= 0 || delta == 0) {
        return false;
    }

    int next = 0;
    if (selectedIndex_ == kNoItem) {
        // 没有选中时：向下从首项开始，向上从末项开始。
        next = delta > 0 ? 0 : itemCount_ - 1;
    } else {
        const long long raw = static_cast<long long>(selectedIndex_) + delta;
        next = raw < 0 ? 0 : (raw >= itemCount_ ? itemCount_ - 1 : static_cast<int>(raw));
    }

    const bool changed = next != selectedIndex_;
    selectedIndex_ = next;
    if (keepVisible) {
        scrollToItem(next, ScrollAlign::Nearest);
    }
    return changed;
}

bool VirtualScroll::ensureSelectedVisible(ScrollAlign align) {
    if (selectedIndex_ == kNoItem) {
        return false;
    }
    return scrollToItem(selectedIndex_, align);
}

// ---------------------------------------------------------------- 滚动条

RectD VirtualScroll::thumbRect(const RectD& track, double minThumbSize) const noexcept {
    if (track.empty()) {
        return {};
    }
    // 内容不足一屏（或没有内容）：滑块占满轨道，表示"没得滚"。
    if (!(totalHeight_ > 0.0) || !(viewportHeight_ > 0.0) || totalHeight_ <= viewportHeight_) {
        return track;
    }

    const double minimum = minThumbSize > 1.0 ? minThumbSize : 1.0;
    if (track.height <= minimum) {
        return track;
    }

    const double ratio = math::clamp(viewportHeight_ / totalHeight_, 0.0, 1.0);
    const double thumbHeight = math::clamp(track.height * ratio, minimum, track.height);
    const double y = track.y + (track.height - thumbHeight) * scrollProgress();
    return {track.x, y, track.width, thumbHeight};
}

}  // namespace kiriyama::core
