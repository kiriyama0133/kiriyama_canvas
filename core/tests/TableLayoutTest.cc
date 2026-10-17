#include <gtest/gtest.h>

#include <vector>

#include "kiriyama/core/TableLayout.h"

namespace {

using kiriyama::core::columnAtX;
using kiriyama::core::computeTableLayout;
using kiriyama::core::fitColumnWidths;
using kiriyama::core::RectD;
using kiriyama::core::rowAtY;
using kiriyama::core::TableLayoutInput;
using kiriyama::core::TableMetrics;

constexpr double kEps = 1e-9;

TableMetrics metrics() {
    TableMetrics m;
    m.headerHeight = 36.0;
    m.rowHeight = 32.0;
    m.paddingX = 14.0;
    return m;
}

TableLayoutInput makeInput(const std::vector<double>& widths, int rows, double viewHeight,
                           double scroll = 0.0) {
    TableLayoutInput in;
    in.columnWidths = widths;
    in.rowCount = rows;
    in.viewWidth = 800.0;
    in.viewHeight = viewHeight;
    in.scrollOffset = scroll;
    in.metrics = metrics();
    return in;
}

// ---------------------------------------------------------------- 列

TEST(TableLayout, ColumnsGetPrefixOffsets) {
    const auto layout = computeTableLayout(makeInput({100.0, 60.0, 40.0}, 10, 320.0));
    ASSERT_EQ(layout.columns.size(), 3u);
    EXPECT_NEAR(layout.columns[0].x, 0.0, kEps);
    EXPECT_NEAR(layout.columns[1].x, 100.0, kEps);
    EXPECT_NEAR(layout.columns[2].x, 160.0, kEps);
    EXPECT_NEAR(layout.columns[1].width, 60.0, kEps);
    EXPECT_NEAR(layout.totalWidth, 200.0, kEps);
}

TEST(TableLayout, NegativeWidthIsTreatedAsZero) {
    // 调用方算错了宽度也不该让后面的列往回叠。
    const auto layout = computeTableLayout(makeInput({-30.0, 80.0}, 3, 96.0));
    EXPECT_NEAR(layout.columns[0].width, 0.0, kEps);
    EXPECT_NEAR(layout.columns[1].x, 0.0, kEps);
    EXPECT_NEAR(layout.totalWidth, 80.0, kEps);
}

// ---------------------------------------------------------------- 滚动

TEST(TableLayout, NoScrollWhenContentFits) {
    const auto layout = computeTableLayout(makeInput({100.0}, 5, 400.0));
    EXPECT_NEAR(layout.contentHeight, 160.0, kEps);
    EXPECT_NEAR(layout.maxScroll, 0.0, kEps);
    EXPECT_NEAR(layout.scroll, 0.0, kEps);
}

TEST(TableLayout, ScrollIsClampedToContent) {
    const auto layout = computeTableLayout(makeInput({100.0}, 20, 200.0, 9999.0));
    EXPECT_NEAR(layout.contentHeight, 640.0, kEps);
    EXPECT_NEAR(layout.maxScroll, 440.0, kEps);
    // 越界的 scrollOffset 必须被夹紧，否则渲染层会画出空白。
    EXPECT_NEAR(layout.scroll, 440.0, kEps);
}

TEST(TableLayout, NegativeScrollBecomesZero) {
    const auto layout = computeTableLayout(makeInput({100.0}, 20, 200.0, -50.0));
    EXPECT_NEAR(layout.scroll, 0.0, kEps);
}

// ---------------------------------------------------------------- 可见范围

TEST(TableLayout, VisibleRangeCoversExactRows) {
    const auto layout = computeTableLayout(makeInput({100.0}, 20, 320.0));
    EXPECT_EQ(layout.firstRow, 0);
    EXPECT_EQ(layout.lastRow, 10);  // 320 / 32
    EXPECT_EQ(layout.visibleRows(), 10);
}

TEST(TableLayout, VisibleRangeIncludesPartialRows) {
    // 100px 的视口放得下 3 整行 + 4px，第 4 行也得画（剩下的交给裁剪）。
    const auto layout = computeTableLayout(makeInput({100.0}, 20, 100.0));
    EXPECT_EQ(layout.firstRow, 0);
    EXPECT_EQ(layout.lastRow, 4);

    // 滚到 40：顶部是第 1 行的一部分，底部第 4 行还有 12px 露出来。
    const auto scrolled = computeTableLayout(makeInput({100.0}, 20, 100.0, 40.0));
    EXPECT_EQ(scrolled.firstRow, 1);
    EXPECT_EQ(scrolled.lastRow, 5);
}

TEST(TableLayout, VisibleRangeStopsAtLastRow) {
    const auto layout = computeTableLayout(makeInput({100.0}, 3, 400.0));
    EXPECT_EQ(layout.firstRow, 0);
    EXPECT_EQ(layout.lastRow, 3);  // 不能算出 12 行
}

TEST(TableLayout, EmptyTableHasNoVisibleRows) {
    const auto layout = computeTableLayout(makeInput({100.0}, 0, 300.0));
    EXPECT_EQ(layout.firstRow, 0);
    EXPECT_EQ(layout.lastRow, 0);
    EXPECT_NEAR(layout.maxScroll, 0.0, kEps);
}

TEST(TableLayout, ZeroHeightViewportDrawsNothing) {
    const auto layout = computeTableLayout(makeInput({100.0}, 10, 0.0));
    EXPECT_EQ(layout.visibleRows(), 0);
}

// ---------------------------------------------------------------- 命中

TEST(TableLayout, ColumnAtXRespectsBoundaries) {
    const auto layout = computeTableLayout(makeInput({100.0, 60.0, 40.0}, 10, 320.0));
    EXPECT_EQ(columnAtX(layout, -0.1), -1);
    EXPECT_EQ(columnAtX(layout, 0.0), 0);
    EXPECT_EQ(columnAtX(layout, 99.9), 0);
    EXPECT_EQ(columnAtX(layout, 100.0), 1);  // 边界归右列
    EXPECT_EQ(columnAtX(layout, 159.9), 1);
    EXPECT_EQ(columnAtX(layout, 199.9), 2);
    EXPECT_EQ(columnAtX(layout, 200.0), -1);  // 恰好是总宽：不属于任何列
}

TEST(TableLayout, RowAtYUsesScroll) {
    const auto layout = computeTableLayout(makeInput({100.0}, 20, 100.0, 40.0));
    EXPECT_EQ(rowAtY(layout, -1.0), -1);
    EXPECT_EQ(rowAtY(layout, 0.0), 1);    // (0 + 40) / 32 = 1
    EXPECT_EQ(rowAtY(layout, 23.9), 1);   // 63.9 / 32 = 1.99
    EXPECT_EQ(rowAtY(layout, 24.0), 2);   // 64 / 32 = 2
}

TEST(TableLayout, RowAtYRejectsOutsideViewport) {
    const auto layout = computeTableLayout(makeInput({100.0}, 20, 100.0));
    EXPECT_EQ(rowAtY(layout, 100.0), -1);  // 视口下沿
    EXPECT_EQ(rowAtY(layout, 99.9), 3);
}

TEST(TableLayout, RowAtYRejectsPastLastRow) {
    const auto layout = computeTableLayout(makeInput({100.0}, 2, 300.0));
    EXPECT_EQ(rowAtY(layout, 32.0), 1);
    EXPECT_EQ(rowAtY(layout, 64.0), -1);   // 第 3 行不存在
    EXPECT_EQ(rowAtY(layout, 200.0), -1);  // 视口内但已无数据
}

// ---------------------------------------------------------------- 矩形

TEST(TableLayout, RowRectAppliesScroll) {
    const RectD body{0.0, 36.0, 800.0, 320.0};
    const auto layout = computeTableLayout(makeInput({100.0}, 20, 320.0, 64.0));
    const RectD row = layout.rowRect(body, 3);
    EXPECT_NEAR(row.x, 0.0, kEps);
    EXPECT_NEAR(row.y, 36.0 + 96.0 - 64.0, kEps);
    EXPECT_NEAR(row.width, 800.0, kEps);
    EXPECT_NEAR(row.height, 32.0, kEps);
}

TEST(TableLayout, CellRectAddsColumnOffset) {
    const RectD body{48.0, 128.0, 800.0, 320.0};
    const auto layout = computeTableLayout(makeInput({100.0, 60.0}, 5, 320.0));
    const RectD cell = layout.cellRect(body, 1, 1);
    EXPECT_NEAR(cell.x, 48.0 + 100.0, kEps);
    EXPECT_NEAR(cell.y, 128.0 + 32.0, kEps);
    EXPECT_NEAR(cell.width, 60.0, kEps);
}

// ---------------------------------------------------------------- 列宽铺满

TEST(FitColumnWidths, GrowsProportionallyToFillWidth) {
    std::vector<double> widths{100.0, 100.0};
    fitColumnWidths(widths, 400.0);
    EXPECT_NEAR(widths[0], 200.0, kEps);
    EXPECT_NEAR(widths[1], 200.0, kEps);
}

TEST(FitColumnWidths, ShrinksRespectingMinWidth) {
    std::vector<double> widths{300.0, 200.0};
    fitColumnWidths(widths, 400.0, 48.0);
    EXPECT_NEAR(widths[0] + widths[1], 400.0, 1e-6);
    EXPECT_GT(widths[0], 48.0);
    EXPECT_GT(widths[1], 48.0);
    // 宽的那列让出更多，比例上保持"谁胖谁减肥"。
    EXPECT_LT(widths[0], 300.0);
}

TEST(FitColumnWidths, KeepsWidthsWhenNothingCanShrink) {
    std::vector<double> widths{40.0, 40.0};
    fitColumnWidths(widths, 50.0, 48.0);
    // 可压缩空间为 0：宁可溢出也不能把列压没。
    EXPECT_NEAR(widths[0], 40.0, kEps);
    EXPECT_NEAR(widths[1], 40.0, kEps);
}

TEST(FitColumnWidths, SplitsEvenlyWhenAllZero) {
    std::vector<double> widths{0.0, 0.0, 0.0, 0.0};
    fitColumnWidths(widths, 400.0);
    for (const double width : widths) {
        EXPECT_NEAR(width, 100.0, kEps);
    }
}

TEST(FitColumnWidths, IgnoresDegenerateInput) {
    std::vector<double> widths{100.0};
    fitColumnWidths(widths, 0.0);
    EXPECT_NEAR(widths[0], 100.0, kEps);

    std::vector<double> empty;
    fitColumnWidths(empty, 400.0);
    EXPECT_TRUE(empty.empty());
}

}  // namespace
