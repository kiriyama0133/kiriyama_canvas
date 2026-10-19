#include "kiriyama/scene/TableWidget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <utility>
#include <variant>

#include "kiriyama/core/SceneDiff.h"

namespace kiriyama::scene {

namespace {

using core::CellType;
using core::Color;
using core::CursorShape;
using core::RectD;
using core::SceneNode;
using core::SortDir;
using core::TableColumn;
using core::TableCell;
using core::TextAlign;
using core::TextBaseline;
using core::TextMeasureFn;
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

[[nodiscard]] double textWidth(const TextMeasureFn& measure, std::string_view text,
                               double fontSize) {
    if (measure) {
        return measure(text, fontSize).width;
    }
    return core::defaultTextMeasure(text, fontSize).width;
}

[[nodiscard]] SceneNode* childByKey(SceneNode& parent, std::string_view key) {
    for (const auto& child : parent.children()) {
        if (child != nullptr && child->key() == key) {
            return child.get();
        }
    }
    return nullptr;
}

void setFill(SceneNode& node, const Color& color, double cornerRadius = 0.0) {
    core::SceneStyle style = node.style();
    style.fill = color;
    style.stroke = Color{};
    style.strokeWidth = 0.0;
    style.cornerRadius = cornerRadius;
    node.setStyle(style);
}

/// 文本节点的公共样式：单行、超宽省略、按列类型对齐、垂直居中。
void applyTextStyle(SceneNode& node, const Color& color, double fontSize, TextAlign align) {
    core::SceneStyle style = node.style();
    style.fill = color;
    style.fontSize = fontSize;
    style.textAlign = align;
    style.textBaseline = TextBaseline::Middle;
    style.maxLines = 1;
    node.setStyle(style);
}

}  // namespace

TableWidget::TableWidget(std::string key) : Widget(std::move(key)) {}

// ---------------------------------------------------------------- 配置 / 数据

void TableWidget::setStyle(const TableStyle& style) {
    style_ = style;
    widthsDirty_ = true;  // 字号等可能变了，之前量的宽度全部作废
    markSceneDirty();
}

void TableWidget::setData(const core::TableData& data) {
    data_ = data;
    state_.attach(data_, ++dataVersion_);
    widthsDirty_ = true;
    hoverPos_ = -1;
    markSceneDirty();
}

// ---------------------------------------------------------------- 排版

Vec2d TableWidget::localPoint(const Vec2d& worldPoint) const noexcept {
    return toLocal(worldPoint) - frame().origin();
}

void TableWidget::relayout() {
    if (widthsDirty_) {
        remeasure();
        widthsDirty_ = false;
        markSceneDirty();
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
    if (data_.columns.empty()) {
        return;
    }

    const double pad = style_.metrics.paddingX;
    widths_.reserve(data_.columns.size());

    for (std::size_t c = 0; c < data_.columns.size(); ++c) {
        const TableColumn& column = data_.columns[c];
        // 表头也算进宽度：否则排序箭头会把标题挤出去。
        double width = textWidth(measure_, column.title, style_.headerFontSize) +
                       (column.sortable ? 14.0 : 0.0);
        for (const core::TableRow& row : data_.rows) {
            const TableCell* cell = row.cell(c);
            if (cell == nullptr || cell->missing()) {
                continue;
            }
            width = std::max(width,
                             textWidth(measure_, formatCell(*cell, column), style_.cellFontSize));
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
    if (layout_.rowCount <= 0 || delta == 0) {
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
    markSceneDirty();
}

void TableWidget::selectFirst() {
    if (layout_.rowCount <= 0) {
        return;
    }
    state_.selectPosition(0);
    scrollRowIntoView(0);
    markSceneDirty();
}

void TableWidget::selectLast() {
    if (layout_.rowCount <= 0) {
        return;
    }
    const int last = layout_.rowCount - 1;
    state_.selectPosition(last);
    scrollRowIntoView(last);
    markSceneDirty();
}

// ---------------------------------------------------------------- 指针

CursorShape TableWidget::cursorAt(const Vec2d& worldPoint) const noexcept {
    if (!hitTest(worldPoint)) {
        return CursorShape::Default;
    }
    const Vec2d local = localPoint(worldPoint);
    if (local.y >= style_.metrics.headerHeight) {
        // 数据行可以点选，但按要求只有可排序表头变手型。
        return CursorShape::Default;
    }
    const int column = core::columnAtX(layout_, local.x);
    if (column >= 0 && static_cast<std::size_t>(column) < data_.columns.size() &&
        data_.columns[static_cast<std::size_t>(column)].sortable) {
        return CursorShape::Pointer;
    }
    return CursorShape::Default;
}

void TableWidget::onUpdate(const FrameContext& ctx) {
    (void)ctx;
    if (frame().x != 0.0 || frame().y != 0.0) {
        setTranslation(frame().origin());
        setFrame({0.0, 0.0, frame().width, frame().height});
    }
    if (!(frame().size() == lastSize_)) {
        lastSize_ = frame().size();
        widthsDirty_ = true;
        markSceneDirty();
    }
    if (widthsDirty_ || sceneDirty_ || scroll_ != lastScroll_) {
        relayout();
    }

    hoverPos_ = -1;
    if (pointerInside()) {
        const Vec2d local = localPoint(pointerPosition());
        if (local.y >= style_.metrics.headerHeight) {
            hoverPos_ = core::rowAtY(layout_, local.y - style_.metrics.headerHeight);
        }
    }

    // 视图状态（滚动 / hover / 选中）变了就重出描述树；reconcile 会把
    // 没变的节点原地留下，只写真正变化的属性。
    if (sceneDirty_ || scroll_ != lastScroll_ || hoverPos_ != lastHover_ ||
        state_.selectedPosition() != lastSelected_) {
        rebuild();
    }
}

void TableWidget::onPress() {
    pressPoint_ = localPoint(pointerPosition());
}

void TableWidget::onRelease(bool inside) {
    if (!inside) {
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
        if (column >= 0 && static_cast<std::size_t>(column) < data_.columns.size()) {
            // 不可排序的列会返回 accepted == false，状态不变。
            state_.cycleSort(data_.columns[static_cast<std::size_t>(column)].id);
            // 排序后行会换位置，原来选中的行可能已经滚出视口。
            if (state_.selectedPosition() >= 0) {
                scrollRowIntoView(state_.selectedPosition());
            }
            markSceneDirty();
        }
        return;
    }

    const int position = core::rowAtY(layout_, local.y - headerHeight);
    if (position >= 0) {
        state_.selectPosition(position);
        markSceneDirty();
    }
}

// ---------------------------------------------------------------- 描述树

SceneNode* TableWidget::plotGroup() {
    // 组件自身持 transform_ 承担位置平移，inner_ 直接挂在组件下作为对账目标。
    if (inner_ == nullptr) {
        auto inner = std::make_unique<SceneNode>(core::NodeType::Group, "table");
        inner_ = inner.get();
        appendChild(std::move(inner));
    }
    return inner_;
}

void TableWidget::rebuild() {
    sceneDirty_ = false;
    lastScroll_ = scroll_;
    lastHover_ = hoverPos_;
    lastSelected_ = state_.selectedPosition();

    SceneNode* target = plotGroup();
    core::SceneNode described{core::NodeType::Group, "table"};
    buildDescription(described);
    // 就地对账：key 相同的节点保身份，只写变化的属性；新行插入、消失行摘除。
    (void)core::reconcileScene(*target, described);
}

void TableWidget::buildDescription(core::SceneNode& plot) {
    const double width = frame().width;
    const double height = frame().height;
    const double headerHeight = style_.metrics.headerHeight;
    const double rowHeight = style_.metrics.rowHeight;
    const double radius = style_.radius;
    const double pad = style_.metrics.paddingX;

    // 卡片底 + 表头底。表头只有上面两角是圆的：圆角矩形打底，
    // 再用一块直角矩形把底部半径那一条补平，最后压一条 1px 分隔线。
    auto card = std::make_unique<SceneNode>(core::NodeType::Rect, "card");
    card->setFrame({0.0, 0.0, width, height});
    setFill(*card, style_.background, radius);
    plot.appendChild(std::move(card));

    auto headerBg = std::make_unique<SceneNode>(core::NodeType::Rect, "header.bg");
    headerBg->setFrame({0.0, 0.0, width, headerHeight});
    setFill(*headerBg, style_.headerBackground, radius);
    plot.appendChild(std::move(headerBg));

    auto headerPatch = std::make_unique<SceneNode>(core::NodeType::Rect, "header.patch");
    headerPatch->setFrame({0.0, headerHeight - radius, width, radius});
    setFill(*headerPatch, style_.headerBackground);
    plot.appendChild(std::move(headerPatch));

    auto headerLine = std::make_unique<SceneNode>(core::NodeType::Rect, "header.line");
    headerLine->setFrame({0.0, headerHeight - 1.0, width, 1.0});
    setFill(*headerLine, style_.border);
    plot.appendChild(std::move(headerLine));

    // 表头文字 + 排序三角。
    for (std::size_t c = 0; c < layout_.columns.size() && c < data_.columns.size(); ++c) {
        const TableColumn& column = data_.columns[c];
        const core::TableColumnSpan& span = layout_.columns[c];
        const bool sorted = state_.isSortedBy(column.id);
        const SortDir direction = state_.directionOf(column.id);
        const double reserved = column.sortable ? 14.0 : 0.0;

        if (!column.title.empty()) {
            auto text = std::make_unique<SceneNode>(core::NodeType::Text,
                                                    "header.cell." + std::to_string(c));
            text->setText(column.title);
            text->setFrame({span.x + pad, 0.0, std::max(0.0, span.width - 2.0 * pad - reserved),
                            headerHeight});
            applyTextStyle(*text, sorted ? style_.headerActiveText : style_.headerText,
                           style_.headerFontSize,
                           isNumeric(column.type) ? TextAlign::Right : TextAlign::Left);
            plot.appendChild(std::move(text));
        }

        if (sorted && direction != SortDir::None) {
            // 小三角比 "▲" 可靠：不依赖字体有没有那个字形。
            const double cx = span.x + span.width - pad - 5.0;
            const double cy = headerHeight * 0.5;
            std::vector<Vec2d> triangle(3);
            if (direction == SortDir::Asc) {
                triangle = {Vec2d{cx - 4.0, cy + 2.5}, Vec2d{cx + 4.0, cy + 2.5},
                            Vec2d{cx, cy - 2.5}};
            } else {
                triangle = {Vec2d{cx - 4.0, cy - 2.5}, Vec2d{cx + 4.0, cy - 2.5},
                            Vec2d{cx, cy + 2.5}};
            }
            auto node = std::make_unique<SceneNode>(core::NodeType::Polygon,
                                                    "header.sort." + std::to_string(c));
            node->setPoints(std::move(triangle));
            setFill(*node, style_.accent);
            plot.appendChild(std::move(node));
        }
    }

    // ---- 可见行。裁剪用节点 clipRect 表达：滚到半行时上下都会被切干净。
    auto body = std::make_unique<SceneNode>(core::NodeType::Group, "body");
    body->setClipRect({0.0, headerHeight, width, std::max(0.0, height - headerHeight)});

    const int selected = state_.selectedPosition();
    for (int position = layout_.firstRow; position < layout_.lastRow; ++position) {
        const std::string rowKey = "row." + std::to_string(position);
        const double y = headerHeight + layout_.rowTop(position);

        if (position == selected || position == hoverPos_) {
            auto bg = std::make_unique<SceneNode>(core::NodeType::Rect, rowKey);
            bg->setFrame({0.0, y, width, rowHeight});
            setFill(*bg, position == selected ? style_.selectedRow : style_.hoverRow);
            body->appendChild(std::move(bg));

            if (position == selected) {
                auto accent = std::make_unique<SceneNode>(core::NodeType::Rect,
                                                          rowKey + ".accent");
                accent->setFrame({0.0, y, 3.0, rowHeight});
                setFill(*accent, style_.accent);
                body->appendChild(std::move(accent));
            }
        }

        auto grid = std::make_unique<SceneNode>(core::NodeType::Rect, rowKey + ".grid");
        grid->setFrame({0.0, y + rowHeight - 1.0, width, 1.0});
        setFill(*grid, style_.grid);
        body->appendChild(std::move(grid));

        const core::TableRow* dataRow = state_.rowAt(position);
        if (dataRow == nullptr) {
            continue;
        }

        for (std::size_t c = 0; c < layout_.columns.size() && c < data_.columns.size(); ++c) {
            const TableColumn& column = data_.columns[c];
            const TableCell* cell = dataRow->cell(c);
            if (cell == nullptr || cell->missing()) {
                continue;
            }
            const std::string value = formatCell(*cell, column);
            if (value.empty()) {
                continue;
            }

            const core::TableColumnSpan& span = layout_.columns[c];
            Color color = style_.rowText;
            if (hasSignFormat(column)) {
                const double number = numericValue(*cell);
                color = number > 0.0 ? style_.positive
                                     : (number < 0.0 ? style_.negative : style_.mutedText);
            }

            auto text = std::make_unique<SceneNode>(core::NodeType::Text,
                                                    rowKey + ".cell." + std::to_string(c));
            text->setText(value);
            text->setFrame({span.x + pad, y, std::max(0.0, span.width - 2.0 * pad), rowHeight});
            applyTextStyle(*text, color, style_.cellFontSize,
                           isNumeric(column.type) ? TextAlign::Right : TextAlign::Left);
            body->appendChild(std::move(text));
        }
    }

    if (layout_.rowCount <= 0) {
        auto empty = std::make_unique<SceneNode>(core::NodeType::Text, "empty");
        empty->setText(data_.columns.empty() ? "no data" : "no rows");
        empty->setFrame({0.0, headerHeight, width, std::max(0.0, height - headerHeight)});
        applyTextStyle(*empty, style_.mutedText, style_.cellFontSize, TextAlign::Center);
        plot.appendChild(std::move(empty));
    }

    // 可见行画完再压卡片外框（与旧绘制顺序一致：边框盖住行内容，表头文字最后）。
    plot.appendChild(std::move(body));

    auto border = std::make_unique<SceneNode>(core::NodeType::Rect, "frame.border");
    border->setFrame({0.0, 0.0, width, height});
    {
        core::SceneStyle style = border->style();
        style.stroke = style_.border;
        style.strokeWidth = 1.0;
        style.cornerRadius = radius;
        border->setStyle(style);
    }
    plot.appendChild(std::move(border));
}

}  // namespace kiriyama::scene
