#include "kiriyama/core/TableLayout.h"

#include <algorithm>
#include <cmath>

namespace kiriyama::core {

RectD TableLayout::rowRect(const RectD& body, int position) const noexcept {
    return RectD{body.x, body.y + rowTop(position), body.width, metrics.rowHeight};
}

RectD TableLayout::cellRect(const RectD& body, int position, std::size_t column) const noexcept {
    const double x = column < columns.size() ? columns[column].x : 0.0;
    const double width = column < columns.size() ? columns[column].width : 0.0;
    return RectD{body.x + x, body.y + rowTop(position), width, metrics.rowHeight};
}

TableLayout computeTableLayout(const TableLayoutInput& input) {
    TableLayout out;
    out.metrics = input.metrics;
    out.rowCount = std::max(0, input.rowCount);

    // 列的横向偏移：前缀和。列宽为负视为 0（调用方给了脏数据也不要算出负宽）。
    double cursor = 0.0;
    out.columns.reserve(input.columnWidths.size());
    for (const double raw : input.columnWidths) {
        const double width = std::max(0.0, raw);
        out.columns.push_back(TableColumnSpan{cursor, width});
        cursor += width;
    }
    out.totalWidth = cursor;

    const double rowHeight = std::max(0.0, out.metrics.rowHeight);
    out.contentHeight = static_cast<double>(out.rowCount) * rowHeight;
    out.viewHeight = std::max(0.0, input.viewHeight);
    out.maxScroll = std::max(0.0, out.contentHeight - out.viewHeight);
    // scrollOffset 可能来自上一帧的数据（行数变少时），这里统一夹紧，
    // 渲染层就不需要到处判断"滚动位置还有效吗"。
    out.scroll = std::clamp(input.scrollOffset, 0.0, out.maxScroll);

    if (out.rowCount == 0 || out.viewHeight <= 0.0 || rowHeight <= 0.0) {
        out.firstRow = 0;
        out.lastRow = 0;
        return out;
    }

    // 首行用 floor：滚到半行时顶部那行仍然要画（哪怕只露出 3px）。
    // 末行用 ceil：底部同理，半行也要画，剩下交给裁剪。
    const int first = static_cast<int>(std::floor(out.scroll / rowHeight));
    const int last = static_cast<int>(std::ceil((out.scroll + out.viewHeight) / rowHeight));
    out.firstRow = std::clamp(first, 0, out.rowCount);
    out.lastRow = std::clamp(last, out.firstRow, out.rowCount);
    return out;
}

int columnAtX(const TableLayout& layout, double x) noexcept {
    if (x < 0.0 || layout.columns.empty()) {
        return -1;
    }
    // 线性扫。列数是个位数到几十，二分不值得，也更容易在"列宽为 0"上出错。
    for (std::size_t i = 0; i < layout.columns.size(); ++i) {
        const TableColumnSpan& span = layout.columns[i];
        if (x >= span.x && x < span.x + span.width) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int rowAtY(const TableLayout& layout, double y) noexcept {
    if (y < 0.0 || y >= layout.viewHeight) {
        return -1;
    }
    const double rowHeight = layout.metrics.rowHeight;
    if (rowHeight <= 0.0) {
        return -1;
    }
    const int position = static_cast<int>(std::floor((y + layout.scroll) / rowHeight));
    return position >= 0 && position < layout.rowCount ? position : -1;
}

void fitColumnWidths(std::vector<double>& widths, double available, double minWidth) {
    if (widths.empty() || available <= 0.0) {
        return;
    }

    // 先把脏数据抹平：负宽会让等比缩放出负数。
    double total = 0.0;
    for (double& width : widths) {
        width = std::max(0.0, width);
        total += width;
    }

    // 全是 0 宽（比如还没测量过）：均分，至少能看见东西。
    if (total <= 0.0) {
        const double each = available / static_cast<double>(widths.size());
        for (double& width : widths) {
            width = each;
        }
        return;
    }

    if (total < available) {
        const double scale = available / total;
        for (double& width : widths) {
            width *= scale;
        }
        return;
    }

    if (total > available) {
        const double excess = total - available;
        double capacity = 0.0;
        for (const double width : widths) {
            capacity += std::max(0.0, width - minWidth);
        }
        if (capacity <= 0.0) {
            return;
        }
        const double take = std::min(excess, capacity);
        for (double& width : widths) {
            width -= std::max(0.0, width - minWidth) / capacity * take;
        }
    }
}

}  // namespace kiriyama::core
