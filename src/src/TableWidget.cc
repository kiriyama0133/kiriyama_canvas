#include "kiriyama/render/TableWidget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <variant>

#include <SDL3/SDL.h>

#include "kiriyama/render/Painter.h"
#include "kiriyama/render/TextRenderer.h"

namespace kiriyama::render {

namespace {

using core::CellType;
using core::Color;
using core::CursorShape;
using core::RectD;
using core::SortDir;
using core::TableColumn;
using core::TableCell;
using core::TextAlign;
using core::Vec2d;

/// 只有数字 / 日期右对齐。文本左对齐是表格的通行约定：
/// 数字右对齐，个位才会一列对齐，比按小数点对齐更省事。
[[nodiscard]] bool isNumeric(CellType type) noexcept {
    return type == CellType::Number || type == CellType::Integer || type == CellType::Date;
}

/// format 里带 '+' 的列（如 "%+.2f%%"）声明了"正负号有意义"，
/// 于是按符号上色（涨红跌绿）。不带 '+' 的列一律正文色。
[[nodiscard]] bool hasSignFormat(const TableColumn& column) noexcept {
    return column.format.find('+') != std::string::npos;
}

[[nodiscard]] double numericValue(const TableCell& cell) noexcept {
    if (const auto* d = std::get_if<double>(&cell.value)) {
        return *d;
    }
    if (const auto* i = std::get_if<std::int64_t>(&cell.value)) {
        return static_cast<double>(*i);
    }
    return 0.0;
}

}  // namespace

TableWidget::TableWidget(std::string key) : Widget(std::move(key)) {
    face_ = Font::builtinFace();
    applyStyle();
}

// ---------------------------------------------------------------- 配置

void TableWidget::setStyle(const TableStyle& style) {
    style_ = style;
    applyStyle();
}

void TableWidget::setFace(std::shared_ptr<const FontFace> face) {
    face_ = face ? std::move(face) : Font::builtinFace();
    applyStyle();
}

void TableWidget::applyStyle() {
    headerFont_ = Font(face_, style_.headerFontSize);
    cellFont_ = Font(face_, style_.cellFontSize);
    // 字号变了，之前量的宽度全部作废。
    widthsDirty_ = true;
}

void TableWidget::setData(const core::TableData* data, std::uint64_t version) {
    data_ = data;
    dataVersion_ = version;
    if (data_ != nullptr) {
        state_.attach(*data_, version);
    } else {
        state_.detach();
    }
    widthsDirty_ = true;
    hoverPos_ = -1;
}

// ---------------------------------------------------------------- 排版

Vec2d TableWidget::origin() const noexcept {
    const RectD box = frame();
    return Vec2d{box.x, box.y};
}

Vec2d TableWidget::localPoint(const Vec2d& worldPoint) const noexcept {
    const Vec2d o = origin();
    return Vec2d{worldPoint.x - o.x, worldPoint.y - o.y};
}

void TableWidget::relayout() {
    if (widthsDirty_) {
        remeasure();
        widthsDirty_ = false;
    }

    const RectD box = frame();
    core::TableLayoutInput input;
    input.columnWidths = widths_;
    input.rowCount = static_cast<int>(state_.rowCount());
    input.viewWidth = box.width;
    input.viewHeight = std::max(0.0, box.height - style_.metrics.headerHeight);
    input.scrollOffset = scroll_;
    input.metrics = style_.metrics;
    layout_ = core::computeTableLayout(input);
    // 行数变少 / 视口变大后，原来的滚动量可能已经越界。表格自己收好这个尾巴，
    // 调用方不用再判断"滚动位置还有效吗"。
    scroll_ = layout_.scroll;
}

void TableWidget::remeasure() {
    widths_.clear();
    if (data_ == nullptr || data_->columns.empty()) {
        return;
    }

    const double pad = style_.metrics.paddingX;
    widths_.reserve(data_->columns.size());

    for (std::size_t c = 0; c < data_->columns.size(); ++c) {
        const TableColumn& column = data_->columns[c];
        // 表头也算进宽度：否则排序箭头会把标题挤出去。
        double width = headerFont_.measure(column.title) + (column.sortable ? 14.0 : 0.0);
        for (const core::TableRow& row : data_->rows) {
            const TableCell* cell = row.cell(c);
            if (cell == nullptr || cell->missing()) {
                continue;
            }
            width = std::max(width, cellFont_.measure(formatCell(*cell, column)));
        }
        width += 2.0 * pad;
        width = std::max(width, column.minWidth);
        // 单个超长文本不该吃掉半张表。
        width = std::min(width, 280.0);
        widths_.push_back(width);
    }

    core::fitColumnWidths(widths_, frame().width, 56.0);
}

std::string TableWidget::formatCell(const TableCell& cell, const TableColumn& column) const {
    if (cell.missing()) {
        return {};
    }

    char buffer[96];
    const std::string& format = column.format;
    const char* pattern = format.empty() ? nullptr : format.c_str();

    switch (column.type) {
        case CellType::Text: {
            if (const auto* text = std::get_if<std::string>(&cell.value)) {
                return *text;
            }
            return {};
        }
        case CellType::Number:
        case CellType::Date: {
            if (const auto* number = std::get_if<double>(&cell.value)) {
                std::snprintf(buffer, sizeof(buffer), pattern ? pattern : "%.2f", *number);
                return buffer;
            }
            if (const auto* integer = std::get_if<std::int64_t>(&cell.value)) {
                std::snprintf(buffer, sizeof(buffer), pattern ? pattern : "%.0f",
                              static_cast<double>(*integer));
                return buffer;
            }
            return {};
        }
        case CellType::Integer: {
            if (const auto* integer = std::get_if<std::int64_t>(&cell.value)) {
                std::snprintf(buffer, sizeof(buffer), pattern ? pattern : "%lld",
                              static_cast<long long>(*integer));
                return buffer;
            }
            if (const auto* number = std::get_if<double>(&cell.value)) {
                std::snprintf(buffer, sizeof(buffer), pattern ? pattern : "%.0f", *number);
                return buffer;
            }
            return {};
        }
        case CellType::Bool: {
            const auto* flag = std::get_if<bool>(&cell.value);
            return (flag != nullptr && *flag) ? "yes" : "no";
        }
    }
    return {};
}

// ---------------------------------------------------------------- 滚动 / 选中

void TableWidget::scrollBy(double delta) {
    relayout();
    scroll_ = std::clamp(scroll_ + delta, 0.0, layout_.maxScroll);
}

void TableWidget::scrollTo(double offset) {
    relayout();
    scroll_ = std::clamp(offset, 0.0, layout_.maxScroll);
}

void TableWidget::scrollRowIntoView(int position) {
    relayout();
    const double rowHeight = style_.metrics.rowHeight;
    const double top = static_cast<double>(position) * rowHeight;
    if (top < scroll_) {
        scroll_ = top;
    } else if (top + rowHeight > scroll_ + layout_.viewHeight) {
        scroll_ = top + rowHeight - layout_.viewHeight;
    }
    scroll_ = std::clamp(scroll_, 0.0, layout_.maxScroll);
}

void TableWidget::moveSelection(int delta) {
    if (data_ == nullptr || layout_.rowCount <= 0 || delta == 0) {
        return;
    }
    // 还没选中时：往下走选第一行，往上走选最后一行（和列表控件的手感一致）。
    int position = state_.selectedPosition();
    if (position < 0) {
        position = delta > 0 ? -1 : layout_.rowCount;
    }
    position = std::clamp(position + delta, 0, layout_.rowCount - 1);
    state_.selectPosition(position);
    scrollRowIntoView(position);
}

void TableWidget::selectFirst() {
    if (layout_.rowCount <= 0) {
        return;
    }
    state_.selectPosition(0);
    scrollRowIntoView(0);
}

void TableWidget::selectLast() {
    if (layout_.rowCount <= 0) {
        return;
    }
    const int last = layout_.rowCount - 1;
    state_.selectPosition(last);
    scrollRowIntoView(last);
}

// ---------------------------------------------------------------- 指针

CursorShape TableWidget::cursorAt(const Vec2d& worldPoint) const noexcept {
    if (data_ == nullptr || !hitTest(worldPoint)) {
        return CursorShape::Default;
    }
    const Vec2d local = localPoint(worldPoint);
    if (local.y >= style_.metrics.headerHeight) {
        // 数据行可以点选，但按要求只有可排序表头变手型。
        return CursorShape::Default;
    }
    const int column = core::columnAtX(layout_, local.x);
    if (column >= 0 && static_cast<std::size_t>(column) < data_->columns.size() &&
        data_->columns[static_cast<std::size_t>(column)].sortable) {
        return CursorShape::Pointer;
    }
    return CursorShape::Default;
}

void TableWidget::onUpdate(const scene::FrameContext& ctx) {
    (void)ctx;
    relayout();

    hoverPos_ = -1;
    if (data_ != nullptr && pointerInside()) {
        const Vec2d local = localPoint(pointerPosition());
        if (local.y >= style_.metrics.headerHeight) {
            hoverPos_ = core::rowAtY(layout_, local.y - style_.metrics.headerHeight);
        }
    }
}

void TableWidget::onPress() {
    pressPoint_ = localPoint(pointerPosition());
}

void TableWidget::onRelease(bool inside) {
    if (!inside || data_ == nullptr) {
        return;
    }
    // 用"松开点"而不是"按下点"做判定：和浏览器按钮的语义一致，
    // 按下后拖出去再松开不算点击（Widget 已经把这种情况过滤成 inside == false）。
    const Vec2d local = localPoint(pointerPosition());
    const double headerHeight = style_.metrics.headerHeight;

    // 按下的过程中指针跑了 6px 以上就当成拖选 / 误触，不触发排序或选中。
    const double dx = local.x - pressPoint_.x;
    const double dy = local.y - pressPoint_.y;
    if (dx * dx + dy * dy > 36.0) {
        return;
    }

    if (local.y < headerHeight) {
        const int column = core::columnAtX(layout_, local.x);
        if (column >= 0 && static_cast<std::size_t>(column) < data_->columns.size()) {
            // 不可排序的列会返回 accepted == false，状态不变。
            state_.cycleSort(data_->columns[static_cast<std::size_t>(column)].id);
            // 排序后行会换位置，原来选中的行可能已经滚出视口。
            if (state_.selectedPosition() >= 0) {
                scrollRowIntoView(state_.selectedPosition());
            }
        }
        return;
    }

    const int position = core::rowAtY(layout_, local.y - headerHeight);
    if (position >= 0) {
        state_.selectPosition(position);
    }
}

// ---------------------------------------------------------------- 绘制

void TableWidget::draw(Painter& painter, SdlTextRenderer& text) {
    relayout();

    const Vec2d o = origin();
    const RectD card{o.x, o.y, frame().width, frame().height};
    const double headerHeight = style_.metrics.headerHeight;
    const double rowHeight = style_.metrics.rowHeight;
    const double radius = style_.radius;
    const double pad = style_.metrics.paddingX;
    const double headerLine = headerFont_.metrics().lineHeight();
    const double cellLine = cellFont_.metrics().lineHeight();

    const RectD body{card.x, card.y + headerHeight, card.width,
                     std::max(0.0, card.height - headerHeight)};

    painter.reset();
    painter.color(style_.background).fillRoundRect(card, radius);

    // 表头底色：先画圆角，再把底部半径那一条补成直角 ——
    // 只有上面两角该是圆的，下面两角和正文接缝必须平。
    painter.color(style_.headerBackground).fillRoundRect(
        RectD{card.x, card.y, card.width, headerHeight}, radius);
    painter.color(style_.headerBackground)
        .fillRect(RectD{card.x, card.y + headerHeight - radius, card.width, radius});
    painter.color(style_.border)
        .fillRect(RectD{card.x, card.y + headerHeight - 1.0, card.width, 1.0});

    // ---- 可见行。裁剪由 SDL 负责：滚到半行时上下都会被切干净。
    const bool hasRows = data_ != nullptr && layout_.rowCount > 0;
    if (hasRows) {
        SDL_Renderer* renderer = painter.renderer();
        const bool clipped = renderer != nullptr;
        if (clipped) {
            SDL_Rect clip{static_cast<int>(std::lround(body.x)),
                          static_cast<int>(std::lround(body.y)),
                          static_cast<int>(std::lround(body.width)),
                          static_cast<int>(std::lround(body.height))};
            (void)SDL_SetRenderClipRect(renderer, &clip);
        }

        const int selected = state_.selectedPosition();
        for (int position = layout_.firstRow; position < layout_.lastRow; ++position) {
            const double y = body.y + layout_.rowTop(position);
            const RectD row{card.x, y, card.width, rowHeight};

            if (position == selected) {
                painter.color(style_.selectedRow).fillRect(row);
                painter.color(style_.accent).fillRect(RectD{card.x, y, 3.0, rowHeight});
            } else if (position == hoverPos_) {
                painter.color(style_.hoverRow).fillRect(row);
            }

            painter.color(style_.grid)
                .fillRect(RectD{row.x, y + rowHeight - 1.0, row.width, 1.0});

            const core::TableRow* dataRow = state_.rowAt(position);
            if (dataRow == nullptr) {
                continue;
            }

            for (std::size_t c = 0; c < layout_.columns.size() && c < data_->columns.size(); ++c) {
                const TableColumn& column = data_->columns[c];
                const TableCell* cell = dataRow->cell(c);
                if (cell == nullptr || cell->missing()) {
                    continue;
                }
                const std::string value = formatCell(*cell, column);
                if (value.empty()) {
                    continue;
                }

                const core::TableColumnSpan& span = layout_.columns[c];
                TextLayoutOptions options;
                options.wrap = false;
                options.maxLines = 1;
                options.ellipsis = "...";
                options.align = isNumeric(column.type) ? TextAlign::Right : TextAlign::Left;
                options.maxWidth = std::max(0.0, span.width - 2.0 * pad);

                Color color = style_.rowText;
                if (hasSignFormat(column)) {
                    const double number = numericValue(*cell);
                    color = number > 0.0 ? style_.positive
                                         : (number < 0.0 ? style_.negative : style_.mutedText);
                }

                text.drawText(value, cellFont_,
                              Vec2d{card.x + span.x + pad, y + (rowHeight - cellLine) * 0.5},
                              options, color);
            }
        }

        if (clipped) {
            (void)SDL_SetRenderClipRect(renderer, nullptr);
        }
    } else {
        TextLayoutOptions options;
        options.wrap = false;
        options.align = TextAlign::Center;
        options.maxWidth = card.width;
        text.drawText(data_ == nullptr ? "no data" : "no rows", cellFont_,
                      Vec2d{card.x, body.y + (body.height - cellLine) * 0.5}, options,
                      style_.mutedText);
    }

    painter.color(style_.border).strokeWidth(1.0).strokeRoundRect(card, radius);

    // ---- 表头文字最后画：行背景和网格线都碰不到表头，但要压在边框之上。
    if (data_ != nullptr) {
        painter.reset();
        for (std::size_t c = 0; c < layout_.columns.size() && c < data_->columns.size(); ++c) {
            const TableColumn& column = data_->columns[c];
            const core::TableColumnSpan& span = layout_.columns[c];
            const bool sorted = state_.isSortedBy(column.id);
            const SortDir direction = state_.directionOf(column.id);
            const double reserved = column.sortable ? 14.0 : 0.0;

            if (!column.title.empty()) {
                TextLayoutOptions options;
                options.wrap = false;
                options.maxLines = 1;
                options.ellipsis = "...";
                options.align = isNumeric(column.type) ? TextAlign::Right : TextAlign::Left;
                options.maxWidth = std::max(0.0, span.width - 2.0 * pad - reserved);

                text.drawText(column.title, headerFont_,
                              Vec2d{card.x + span.x + pad,
                                    card.y + (headerHeight - headerLine) * 0.5},
                              options,
                              sorted ? style_.headerActiveText : style_.headerText);
            }

            if (sorted && direction != SortDir::None) {
                // 小三角比 "▲" 可靠：不依赖字体有没有那个字形。
                const double cx = card.x + span.x + span.width - pad - 5.0;
                const double cy = card.y + headerHeight * 0.5;
                std::array<Vec2d, 3> triangle{};
                if (direction == SortDir::Asc) {
                    triangle = std::array<Vec2d, 3>{Vec2d{cx - 4.0, cy + 2.5},
                                                    Vec2d{cx + 4.0, cy + 2.5},
                                                    Vec2d{cx, cy - 2.5}};
                } else {
                    triangle = std::array<Vec2d, 3>{Vec2d{cx - 4.0, cy - 2.5},
                                                    Vec2d{cx + 4.0, cy - 2.5},
                                                    Vec2d{cx, cy + 2.5}};
                }
                painter.color(style_.accent).fillPolygon(triangle);
            }
        }
    }
}

}  // namespace kiriyama::render
