#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "kiriyama/core/Chart.h"

namespace {

using kiriyama::core::ItemRange;
using kiriyama::core::kNoItem;
using kiriyama::core::RectD;
using kiriyama::core::ScrollAlign;
using kiriyama::core::VirtualScroll;
using kiriyama::core::VirtualScrollOptions;

/// 100 项 / 行高 20 / 视口 100：可见 5 项，内容高 2000，最大偏移 1900。
VirtualScroll makeList(const VirtualScrollOptions& options = {}) {
    return VirtualScroll(100, 20.0, 100.0, options);
}

TEST(VirtualScroll, EmptyListIsSafe) {
    VirtualScroll list;

    EXPECT_EQ(list.itemCount(), 0);
    EXPECT_DOUBLE_EQ(list.totalHeight(), 0.0);
    EXPECT_DOUBLE_EQ(list.maxScrollOffset(), 0.0);
    EXPECT_FALSE(list.scrollable());
    EXPECT_TRUE(list.renderRange().empty());
    EXPECT_TRUE(list.visibleRange().empty());

    EXPECT_EQ(list.indexAtContentY(0.0), kNoItem);
    EXPECT_EQ(list.indexAtViewportY(0.0), kNoItem);
    EXPECT_EQ(list.selectedIndex(), kNoItem);
    EXPECT_TRUE(list.itemRect(0, RectD{0.0, 0.0, 200.0, 100.0}).empty());

    EXPECT_FALSE(list.setScrollOffset(50.0));
    EXPECT_FALSE(list.moveSelection(1));
}

TEST(VirtualScroll, BasicGeometry) {
    const VirtualScroll list = makeList();

    EXPECT_EQ(list.itemCount(), 100);
    EXPECT_DOUBLE_EQ(list.itemHeight(), 20.0);
    EXPECT_DOUBLE_EQ(list.totalHeight(), 2000.0);
    EXPECT_DOUBLE_EQ(list.maxScrollOffset(), 1900.0);
    EXPECT_TRUE(list.scrollable());

    EXPECT_DOUBLE_EQ(list.itemTop(0), 0.0);
    EXPECT_DOUBLE_EQ(list.itemTop(3), 60.0);
    EXPECT_DOUBLE_EQ(list.itemBottom(3), 80.0);
    EXPECT_DOUBLE_EQ(list.itemTop(100), 2000.0);  // 末项之后 == 内容高度
    EXPECT_DOUBLE_EQ(list.itemTop(-5), 0.0);      // 负数夹到顶部

    const RectD viewport{10.0, 40.0, 300.0, 100.0};
    const RectD rect = list.itemRect(3, viewport);
    EXPECT_DOUBLE_EQ(rect.x, 10.0);
    EXPECT_DOUBLE_EQ(rect.y, 40.0 + 60.0);
    EXPECT_DOUBLE_EQ(rect.width, 300.0);
    EXPECT_DOUBLE_EQ(rect.height, 20.0);
}

TEST(VirtualScroll, ItemRectFollowsScrollOffset) {
    VirtualScroll list = makeList();
    list.setScrollOffset(30.0);

    const RectD viewport{0.0, 0.0, 200.0, 100.0};
    // 第 3 项内容坐标 60，减掉偏移 30 -> 视口内 30
    EXPECT_DOUBLE_EQ(list.itemRect(3, viewport).y, 30.0);
    // 第 1 项被推到视口上方（负坐标，交给渲染层裁剪）
    EXPECT_DOUBLE_EQ(list.itemRect(1, viewport).y, -10.0);
}

TEST(VirtualScroll, VisibleRangeExcludesItemStartingAtBottomEdge) {
    const VirtualScroll list = makeList();

    // 视口 [0, 100)，行高 20 -> 0..4，第 5 项正好顶在底边，不算可见
    const ItemRange range = list.visibleRange();
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.count, 5);
    EXPECT_EQ(range.end(), 5);
    EXPECT_TRUE(range.contains(4));
    EXPECT_FALSE(range.contains(5));
}

TEST(VirtualScroll, VisibleRangeIncludesPartiallyClippedItems) {
    VirtualScroll list = makeList();
    list.setScrollOffset(30.0);

    // [30, 130) -> 首项 1（顶部被裁 10px），末项 ceil(6.5)=7 排他 -> 1..6
    const ItemRange range = list.visibleRange();
    EXPECT_EQ(range.first, 1);
    EXPECT_EQ(range.count, 6);
    EXPECT_EQ(range.end(), 7);
}

TEST(VirtualScroll, VisibleRangeAtExactBoundary) {
    VirtualScroll list = makeList();
    list.setScrollOffset(20.0);

    // [20, 120) 正好落在整行边界上 -> 1..5
    const ItemRange range = list.visibleRange();
    EXPECT_EQ(range.first, 1);
    EXPECT_EQ(range.count, 5);
}

TEST(VirtualScroll, RenderRangeAddsBuffers) {
    VirtualScrollOptions options;
    options.bufferItems = 3;
    VirtualScroll list = makeList(options);

    // 顶部：上方没有可缓冲的，只多出下方 3 项 -> 0..8
    ItemRange range = list.renderRange();
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.count, 8);

    // 底部：可见 95..99，上方补 3 项 -> 92..100
    list.scrollToEnd();
    range = list.renderRange();
    EXPECT_EQ(range.first, 92);
    EXPECT_EQ(range.count, 8);
    EXPECT_EQ(range.end(), 100);

    // 中部：可见 50..54（偏移 1000），上下各 3 -> 47..58
    list.setScrollOffset(1000.0);
    range = list.renderRange();
    EXPECT_EQ(range.first, 47);
    EXPECT_EQ(range.count, 11);
}

TEST(VirtualScroll, RenderRangeAddsPixelBuffer) {
    VirtualScrollOptions options;
    options.bufferItems = 1;
    options.bufferPixels = 30.0;  // 行高 20 -> 再多 2 项
    VirtualScroll list = makeList(options);
    list.setScrollOffset(1000.0);

    // 可见 50..54，缓冲 1 + ceil(30/20)=2 -> 上下各 3 -> 47..58
    const ItemRange range = list.renderRange();
    EXPECT_EQ(range.first, 47);
    EXPECT_EQ(range.count, 11);
}

TEST(VirtualScroll, RangeForOffsetDoesNotMoveTheList) {
    const VirtualScroll list = makeList();

    const ItemRange preview = list.rangeForOffset(1000.0);
    EXPECT_EQ(preview.first, 47);
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 0.0);

    // 越界偏移先被夹取再计算
    EXPECT_EQ(list.rangeForOffset(-100.0).first, 0);
    EXPECT_EQ(list.rangeForOffset(1e9).first, 92);
}

TEST(VirtualScroll, ScrollOffsetIsClamped) {
    VirtualScroll list = makeList();

    EXPECT_TRUE(list.setScrollOffset(500.0));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 500.0);

    EXPECT_TRUE(list.setScrollOffset(1e9));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 1900.0);

    EXPECT_TRUE(list.setScrollOffset(-1.0));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 0.0);

    // NaN / inf 当 0 处理，绝不让脏值流进渲染
    list.setScrollOffset(700.0);
    EXPECT_TRUE(list.setScrollOffset(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 0.0);
    list.setScrollOffset(700.0);
    EXPECT_TRUE(list.setScrollOffset(std::numeric_limits<double>::infinity()));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 1900.0);

    // 没变化时返回 false，可以直接用来决定要不要重绘
    EXPECT_FALSE(list.setScrollOffset(1900.0));
    EXPECT_FALSE(list.scrollBy(10.0));
}

TEST(VirtualScroll, ScrollByAndEdges) {
    VirtualScroll list = makeList();

    EXPECT_TRUE(list.scrollBy(120.0));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 120.0);

    EXPECT_TRUE(list.scrollToEnd());
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 1900.0);
    EXPECT_FALSE(list.scrollToEnd());

    EXPECT_TRUE(list.scrollToStart());
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 0.0);
    EXPECT_FALSE(list.scrollToStart());

    // 内容不足一屏：怎么滚都是 0
    VirtualScroll shortList(3, 20.0, 100.0);
    EXPECT_FALSE(shortList.scrollable());
    EXPECT_FALSE(shortList.scrollBy(50.0));
    EXPECT_DOUBLE_EQ(shortList.scrollOffset(), 0.0);
}

TEST(VirtualScroll, ScrollProgress) {
    VirtualScroll list = makeList();
    EXPECT_DOUBLE_EQ(list.scrollProgress(), 0.0);
    list.setScrollOffset(950.0);
    EXPECT_DOUBLE_EQ(list.scrollProgress(), 0.5);
    list.scrollToEnd();
    EXPECT_DOUBLE_EQ(list.scrollProgress(), 1.0);

    const VirtualScroll shortList(3, 20.0, 100.0);
    EXPECT_DOUBLE_EQ(shortList.scrollProgress(), 0.0);
}

TEST(VirtualScroll, IndexAtContentY) {
    const VirtualScroll list = makeList();

    EXPECT_EQ(list.indexAtContentY(0.0), 0);
    EXPECT_EQ(list.indexAtContentY(19.999), 0);
    EXPECT_EQ(list.indexAtContentY(20.0), 1);
    EXPECT_EQ(list.indexAtContentY(1999.9), 99);

    // 内容范围内之外一律 kNoItem，调用方不用自己判边界
    EXPECT_EQ(list.indexAtContentY(-0.001), kNoItem);
    EXPECT_EQ(list.indexAtContentY(2000.0), kNoItem);
    EXPECT_EQ(list.indexAtContentY(5000.0), kNoItem);
    EXPECT_EQ(list.indexAtContentY(std::nan("")), kNoItem);
}

TEST(VirtualScroll, IndexAtViewportYAccountsForOffset) {
    VirtualScroll list = makeList();
    list.setScrollOffset(30.0);

    EXPECT_EQ(list.indexAtViewportY(0.0), 1);     // 内容 y = 30
    EXPECT_EQ(list.indexAtViewportY(9.9), 1);
    EXPECT_EQ(list.indexAtViewportY(10.0), 2);    // 内容 y = 40

    // 视口外：上方是内容顶部之上
    list.scrollToStart();
    EXPECT_EQ(list.indexAtViewportY(-1.0), kNoItem);

    // 视口底边之下、内容之内，仍然能反查到项
    EXPECT_EQ(list.indexAtViewportY(150.0), 7);
}

TEST(VirtualScroll, ScrollToItemAlignments) {
    VirtualScroll list = makeList();

    EXPECT_TRUE(list.scrollToItem(10, ScrollAlign::Start));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 200.0);

    EXPECT_TRUE(list.scrollToItem(10, ScrollAlign::Center));
    // 项中心 210 == 视口中心 offset + 50
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 160.0);
    EXPECT_NEAR(list.itemTop(10) + 10.0, list.scrollOffset() + 50.0, 1e-9);

    EXPECT_TRUE(list.scrollToItem(10, ScrollAlign::End));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 120.0);
    EXPECT_NEAR(list.itemBottom(10), list.scrollOffset() + 100.0, 1e-9);

    // 已经完全可见时 Nearest 不动
    EXPECT_FALSE(list.scrollToItem(10, ScrollAlign::Nearest));

    // 上方不可见 -> 顶对齐；下方不可见 -> 底对齐
    EXPECT_TRUE(list.scrollToItem(0, ScrollAlign::Nearest));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 0.0);
    EXPECT_TRUE(list.scrollToItem(50, ScrollAlign::Nearest));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 50.0 * 20.0 + 20.0 - 100.0);

    EXPECT_FALSE(list.scrollToItem(-1));
    EXPECT_FALSE(list.scrollToItem(100));
}

TEST(VirtualScroll, ScrollToItemNearEndClamps) {
    VirtualScroll list = makeList();

    // 末项 Center 算出来会越过上界，夹到 maxScrollOffset
    EXPECT_TRUE(list.scrollToItem(99, ScrollAlign::Center));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 1900.0);

    EXPECT_TRUE(list.scrollToItem(0, ScrollAlign::End));
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 0.0);
}

TEST(VirtualScroll, SelectionSurvivesScrolling) {
    VirtualScroll list = makeList();
    list.setSelectedIndex(5);

    EXPECT_EQ(list.selectedIndex(), 5);
    EXPECT_TRUE(list.isSelected(5));
    EXPECT_TRUE(list.isSelectedVisible());

    list.scrollToEnd();
    // 选中项滚出屏幕，但记录不丢
    EXPECT_EQ(list.selectedIndex(), 5);
    EXPECT_TRUE(list.isSelected(5));
    EXPECT_FALSE(list.isSelectedVisible());

    // 需要高亮时再滚回来
    EXPECT_TRUE(list.ensureSelectedVisible(ScrollAlign::Center));
    EXPECT_TRUE(list.isSelectedVisible());
}

TEST(VirtualScroll, SetSelectedIndexClampsAndClears) {
    VirtualScroll list = makeList();

    list.setSelectedIndex(1000);
    EXPECT_EQ(list.selectedIndex(), 99);

    list.setSelectedIndex(-1);
    EXPECT_EQ(list.selectedIndex(), kNoItem);
    EXPECT_FALSE(list.hasSelection());
    EXPECT_FALSE(list.ensureSelectedVisible());

    list.setSelectedIndex(3);
    EXPECT_TRUE(list.hasSelection());
}

TEST(VirtualScroll, SelectionIsDroppedWhenCountShrinks) {
    VirtualScroll list = makeList();
    list.setSelectedIndex(50);

    list.setItemCount(30);
    EXPECT_EQ(list.selectedIndex(), kNoItem);
    EXPECT_FALSE(list.hasSelection());

    // 收缩后偏移也要夹回新的上界（30 * 20 - 100 = 500）
    list.scrollToEnd();
    list.setItemCount(10);
    EXPECT_DOUBLE_EQ(list.maxScrollOffset(), 100.0);
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 100.0);
}

TEST(VirtualScroll, MoveSelectionSkipsFromEdges) {
    VirtualScroll list = makeList();

    // 没有选中时：下移从首项开始
    EXPECT_TRUE(list.moveSelection(1));
    EXPECT_EQ(list.selectedIndex(), 0);

    // 上移从末项开始（先清空再上移）
    list.setSelectedIndex(-1);
    EXPECT_TRUE(list.moveSelection(-1));
    EXPECT_EQ(list.selectedIndex(), 99);

    // 边界上再移不产生变化
    EXPECT_FALSE(list.moveSelection(1));
    EXPECT_EQ(list.selectedIndex(), 99);
    list.setSelectedIndex(0);
    EXPECT_FALSE(list.moveSelection(-1));
    EXPECT_EQ(list.selectedIndex(), 0);
}

TEST(VirtualScroll, MoveSelectionKeepsItemVisible) {
    VirtualScroll list = makeList();
    list.setSelectedIndex(0);

    // 视口能看到 0..4，选中到 10 时必须跟着滚
    for (int i = 0; i < 10; ++i) {
        list.moveSelection(1);
    }
    EXPECT_EQ(list.selectedIndex(), 10);
    EXPECT_TRUE(list.renderRange().contains(10));
    EXPECT_TRUE(list.isSelectedVisible());

    // keepVisible = false 时只改选中，不动滚动
    list.setScrollOffset(0.0);
    list.moveSelection(1, false);
    EXPECT_EQ(list.selectedIndex(), 11);
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 0.0);
}

TEST(VirtualScroll, ItemCountGrowthKeepsOffset) {
    VirtualScroll list = makeList();
    list.scrollToEnd();
    ASSERT_DOUBLE_EQ(list.scrollOffset(), 1900.0);

    list.setItemCount(200);
    EXPECT_DOUBLE_EQ(list.totalHeight(), 4000.0);
    EXPECT_DOUBLE_EQ(list.maxScrollOffset(), 3900.0);
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 1900.0);

    // 追加数据后想继续贴底，要显式再调一次
    EXPECT_TRUE(list.scrollToEnd());
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 3900.0);
}

TEST(VirtualScroll, ViewportResizeKeepsBottomWhenScrolledToEnd) {
    VirtualScroll list = makeList();
    list.scrollToEnd();
    ASSERT_DOUBLE_EQ(list.scrollOffset(), 1900.0);

    list.setViewportHeight(300.0);
    EXPECT_DOUBLE_EQ(list.maxScrollOffset(), 1700.0);
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 1700.0);
    // 末项底边仍然贴着视口底
    EXPECT_NEAR(list.itemBottom(99) - list.scrollOffset(), 300.0, 1e-9);

    // 视口变矮：偏移不动，只是能滚得更远
    list.setScrollOffset(500.0);
    list.setViewportHeight(100.0);
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 500.0);
    EXPECT_TRUE(list.visibleRange().contains(list.indexAtContentY(500.0)));
}

TEST(VirtualScroll, FromTotalHeightDerivesItemHeight) {
    const VirtualScroll list =
        VirtualScroll::FromTotalHeight(10, 500.0, 100.0);

    EXPECT_EQ(list.itemCount(), 10);
    EXPECT_DOUBLE_EQ(list.itemHeight(), 50.0);
    EXPECT_DOUBLE_EQ(list.totalHeight(), 500.0);
    EXPECT_DOUBLE_EQ(list.maxScrollOffset(), 400.0);
    EXPECT_NEAR(list.itemTop(7), 350.0, 1e-9);
}

TEST(VirtualScroll, SetTotalHeightAndItemHeight) {
    VirtualScroll list = makeList();

    list.setTotalHeight(1000.0);
    EXPECT_DOUBLE_EQ(list.itemHeight(), 10.0);
    EXPECT_DOUBLE_EQ(list.totalHeight(), 1000.0);
    EXPECT_DOUBLE_EQ(list.maxScrollOffset(), 900.0);

    list.setItemHeight(40.0);
    EXPECT_DOUBLE_EQ(list.totalHeight(), 4000.0);

    // 非法行高被忽略，几何保持不变
    list.setItemHeight(0.0);
    EXPECT_DOUBLE_EQ(list.itemHeight(), 40.0);
    list.setItemHeight(std::nan(""));
    EXPECT_DOUBLE_EQ(list.itemHeight(), 40.0);
    list.setItemHeight(-5.0);
    EXPECT_DOUBLE_EQ(list.itemHeight(), 40.0);

    // 空列表设总高度无意义，忽略
    VirtualScroll empty;
    empty.setTotalHeight(500.0);
    EXPECT_DOUBLE_EQ(empty.totalHeight(), 0.0);
}

TEST(VirtualScroll, InvalidGeometryDegradesInsteadOfThrowing) {
    // 行高为 0：范围为空，但不崩
    VirtualScroll zeroHeight(10, 0.0, 100.0);
    EXPECT_FALSE(zeroHeight.scrollable());
    EXPECT_TRUE(zeroHeight.renderRange().empty());
    EXPECT_EQ(zeroHeight.indexAtContentY(10.0), kNoItem);

    // 负的视口高度按 0 处理：渲染范围为空，但内容仍然"溢出"，偏移上界等于内容高
    const VirtualScroll negativeViewport(10, 20.0, -50.0);
    EXPECT_DOUBLE_EQ(negativeViewport.viewportHeight(), 0.0);
    EXPECT_TRUE(negativeViewport.renderRange().empty());
    EXPECT_DOUBLE_EQ(negativeViewport.maxScrollOffset(), 200.0);

    // 负条数按 0 处理
    const VirtualScroll negativeCount(-10, 20.0, 100.0);
    EXPECT_EQ(negativeCount.itemCount(), 0);
}

TEST(VirtualScroll, ShortContentRendersEverything) {
    const VirtualScroll list(3, 20.0, 100.0);

    const ItemRange range = list.renderRange();
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.count, 3);
    EXPECT_FALSE(list.scrollable());
    EXPECT_EQ(list.indexAtContentY(59.0), 2);
    EXPECT_EQ(list.indexAtContentY(60.0), kNoItem);
}

TEST(VirtualScroll, OptionsCanBeReplaced) {
    VirtualScroll list = makeList();
    list.setScrollOffset(1000.0);

    ASSERT_EQ(list.renderRange().first, 47);

    VirtualScrollOptions options;
    options.bufferItems = 0;
    list.setOptions(options);

    const ItemRange range = list.renderRange();
    EXPECT_EQ(range.first, 50);
    EXPECT_EQ(range.count, 5);
    EXPECT_DOUBLE_EQ(list.scrollOffset(), 1000.0);
}

TEST(VirtualScroll, ThumbRectFillsTrackWhenNotScrollable) {
    const VirtualScroll list(3, 20.0, 100.0);
    const RectD track{0.0, 10.0, 8.0, 100.0};

    const RectD thumb = list.thumbRect(track);
    EXPECT_EQ(thumb, track);
}

TEST(VirtualScroll, ThumbRectProportionalAndPinnedAtEnds) {
    VirtualScroll list = makeList();
    const RectD track{0.0, 10.0, 8.0, 100.0};

    // 视口 100 / 内容 2000 = 5%，但受最小尺寸 24 约束
    RectD thumb = list.thumbRect(track, 24.0);
    EXPECT_DOUBLE_EQ(thumb.height, 24.0);
    EXPECT_DOUBLE_EQ(thumb.y, 10.0);

    list.setScrollOffset(950.0);  // 进度 0.5
    thumb = list.thumbRect(track, 24.0);
    EXPECT_DOUBLE_EQ(thumb.y, 10.0 + (100.0 - 24.0) * 0.5);

    list.scrollToEnd();
    thumb = list.thumbRect(track, 24.0);
    EXPECT_DOUBLE_EQ(thumb.y + thumb.height, 110.0);

    // 不给最小尺寸时按真实比例：5% -> 5px
    thumb = list.thumbRect(track, 0.0);
    EXPECT_DOUBLE_EQ(thumb.height, 5.0);

    // 空轨道
    EXPECT_TRUE(list.thumbRect(RectD{}).empty());
}

TEST(VirtualScroll, RangeIterationIsHalfOpen) {
    VirtualScrollOptions options;
    options.bufferItems = 0;
    VirtualScroll list = makeList(options);
    list.setScrollOffset(40.0);

    int visited = 0;
    for (int i = list.renderRange().first; i < list.renderRange().end(); ++i) {
        EXPECT_TRUE(list.renderRange().contains(i));
        ++visited;
    }
    // [40, 140) -> 2..6
    EXPECT_EQ(visited, 5);
    EXPECT_EQ(list.renderRange().first, 2);
}

}  // namespace
