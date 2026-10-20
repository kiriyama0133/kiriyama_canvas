#pragma once

// 表格几何：列位置、行位置、可见范围、命中。
//
// 和 TableState 一样是纯逻辑：输入是"列宽 + 视口 + 滚动量"，输出全是可测的数值，
// 一行 SDL 都没有。渲染层只是把这些矩形照抄到画笔上。
//
// 坐标约定：全部是**表格自身坐标系**，原点是卡片左上角。
// 表头占据 [0, headerHeight)，正文从 headerHeight 开始，行高固定。
// 变高行不在这一版的范围内 —— VirtualScroll 也是均匀行高假设。

#include <cstddef>
#include <span>
#include <vector>

#include "Geometry.h"
#include "Layout.h"
#include "Table.h"

namespace kiriyama::core {

struct TableMetrics {
    double headerHeight{36.0};
    double rowHeight{32.0};
    double paddingX{14.0};
};

/// 一列的横向占位。x 是相对表格左边，不是绝对坐标。
struct TableColumnSpan {
    double x{0.0};
    double width{0.0};
};

struct TableLayout {
    std::vector<TableColumnSpan> columns{};
    TableMetrics metrics{};

    double totalWidth{0.0};     ///< 所有列宽之和
    double contentHeight{0.0};  ///< rowCount * rowHeight（完整内容高度）
    double viewHeight{0.0};     ///< 正文可见高度（不含表头）
    double maxScroll{0.0};
    double scroll{0.0};  ///< 已夹紧到 [0, maxScroll]

    int rowCount{0};
    int firstRow{0};  ///< 含。滚到半行时这是顶部那半行
    int lastRow{0};   ///< 不含

    [[nodiscard]] bool empty() const noexcept { return columns.empty(); }
    [[nodiscard]] int visibleRows() const noexcept { return lastRow - firstRow; }

    /// 行顶边在逻辑内容空间中的 y（未扣滚动，供 ScrollWidget::content 等平移容器使用）。
    [[nodiscard]] double rowContentTop(int position) const noexcept {
        return static_cast<double>(position) * metrics.rowHeight;
    }

    /// 行顶边相对正文顶部的 y（已扣滚动）。可能为负或超出 viewHeight。
    [[nodiscard]] double rowTop(int position) const noexcept {
        return rowContentTop(position) - scroll;
    }

    /// 把任意滚动偏移夹紧到 [0, maxScroll]。
    [[nodiscard]] double clampScroll(double offset) const noexcept;

    /// 算出让指定显示行完整滚入可见区所需的新 scroll 值（已夹紧到 [0, maxScroll]）。
    [[nodiscard]] double scrollToRevealRow(int position) const noexcept;

    /// 行矩形。body 是正文区域（不含表头）。
    [[nodiscard]] RectD rowRect(const RectD& body, int position) const noexcept;
    [[nodiscard]] RectD cellRect(const RectD& body, int position, std::size_t column) const noexcept;
};

struct TableLayoutInput {
    std::span<const double> columnWidths{};
    int rowCount{0};
    double viewWidth{0.0};
    /// 正文高度，不含表头。
    double viewHeight{0.0};
    double scrollOffset{0.0};
    TableMetrics metrics{};
};

[[nodiscard]] TableLayout computeTableLayout(const TableLayoutInput& input);

/// x 相对表格左边。落在列外（含 x == totalWidth）返回 -1。
[[nodiscard]] int columnAtX(const TableLayout& layout, double x) noexcept;

/// y 相对正文顶部（表头下沿），内部自己加滚动。越界返回 -1。
[[nodiscard]] int rowAtY(const TableLayout& layout, double y) noexcept;

/// 把列宽铺满 available：
///   不足 → 按比例放大（表格右侧不留空白）；
///   超出 → 按每列"可压缩空间"（width - minWidth）等比压缩，不把任何列压到 minWidth 以下。
/// 没有可压缩空间时保持原样（宁可溢出，也不把列挤成一条缝）。
void fitColumnWidths(std::vector<double>& widths, double available, double minWidth = 48.0);

/// 自动测量列宽（表头 + 所有单元格显示串宽度 + 左右内边距）并按 availableWidth 伸缩铺满。
[[nodiscard]] std::vector<double> measureTableColumns(const TableData& data,
                                                      double availableWidth,
                                                      double headerFontSize,
                                                      double cellFontSize,
                                                      double paddingX,
                                                      const TextMeasureFn& measure = {});

}  // namespace kiriyama::core
