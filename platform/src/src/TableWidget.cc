#include "kiriyama/scene/TableWidget.h"

#include <algorithm>
#include <span>
#include <utility>

#include "kiriyama/core/SceneBuilder.h"
#include "kiriyama/core/SceneDiff.h"

namespace kiriyama::scene {

using core::CursorShape;
using core::RectD;
using core::SceneNode;
using core::TableCell;
using core::TableColumn;
using core::Vec2d;

TableWidget::TableWidget(std::string key) : Widget(std::move(key)) {
    // 1. 底层：卡片底 + 固定表头（列标题随横向滚动同步平移）+ 空数据提示
    auto chrome = std::make_unique<SceneNode>(core::NodeType::Group, "table.chrome");
    chromeGroup_ = chrome.get();
    appendChild(std::move(chrome));

    // 2. 中层：正文滚动容器 ScrollWidget（统一托管滚轮、滑块 Drag 拖拽、轨道点击与横纵裁剪）
    auto scroll = std::make_unique<ScrollWidget>("table.scroll");
    ScrollConfig cfg;
    cfg.overflowX = core::Overflow::Auto;
    cfg.overflowY = core::Overflow::Auto;
    cfg.overlayScrollbars = true;
    cfg.wheelStep = 36.0;
    scroll->setConfig(cfg);
    scroll->setAutoContentSize(false);

    auto body = std::make_unique<SceneNode>(core::NodeType::Group, "body");
    bodyGroup_ = body.get();
    scroll->addContentChild(std::move(body));

    scroll->onScroll([this](Vec2d offset, Vec2d /*progress*/) {
        onScrollChanged(offset);
    });
    scroll_ = scroll.get();
    appendChild(std::move(scroll));

    // 3. 顶层：卡片整体外边框（压在表头与滚动容器之上，保持圆角闭合）
    auto border = std::make_unique<SceneNode>(core::NodeType::Rect, "frame.border");
    borderNode_ = border.get();
    appendChild(std::move(border));

    // 4. 隧道阶段（Tunnel / Capture）优先捕获上下/翻页/首尾键驱动表格选中行移动，
    //    消费后自动阻断向子 ScrollWidget 传播；当指针在表头区域滚轮时转发给 ScrollWidget。
    captureKeyDown(KeyCode::Down, [this] { moveSelection(1); }, KeyMod::Any);
    captureKeyDown(KeyCode::Up, [this] { moveSelection(-1); }, KeyMod::Any);
    captureKeyDown(KeyCode::PageDown, [this] { moveSelection(9); }, KeyMod::Any);
    captureKeyDown(KeyCode::PageUp, [this] { moveSelection(-9); }, KeyMod::Any);
    captureKeyDown(KeyCode::Home, [this] { selectFirst(); }, KeyMod::Any);
    captureKeyDown(KeyCode::End, [this] { selectLast(); }, KeyMod::Any);
    bindWheel([this](const WheelEvent& event) {
        return scroll_ != nullptr && scroll_->dispatchWheel(event);
    });
}

// ---------------------------------------------------------------- 配置 / 数据

void TableWidget::setStyle(const TableStyle& style) {
    style_ = style;
    core::ScrollBarStyle barStyle = scroll_->barStyle();
    barStyle.thumbActiveFill = style_.accent;
    scroll_->setBarStyle(barStyle);
    widthsDirty_ = true;
    sceneDirty_ = true;
}

void TableWidget::setData(core::TableData data) {
    data_ = std::move(data);
    state_.attach(data_, ++dataVersion_);
    widthsDirty_ = true;
    hoverPos_ = -1;
    sceneDirty_ = true;
}

std::string TableWidget::formatCell(const TableCell& cell, const TableColumn& column) const {
    return core::formatTableCell(cell, column);
}

// ---------------------------------------------------------------- 滚动 / 选中（委托给 ScrollWidget 与 TableLayout）

Vec2d TableWidget::localPoint(const Vec2d& worldPoint) const noexcept {
    return toLocal(worldPoint) - frame().origin();
}

void TableWidget::ensureLayout() {
    if (widthsDirty_ || sceneDirty_) {
        rebuild();
    }
}

void TableWidget::scrollBy(double delta) {
    ensureLayout();
    scroll_->scrollBy({0.0, delta});
}

void TableWidget::scrollTo(double offset) {
    ensureLayout();
    scroll_->setScrollY(offset);
}

void TableWidget::moveSelection(int delta) {
    ensureLayout();
    if (state_.moveSelection(delta).accepted) {
        scroll_->setScrollY(layout_.scrollToRevealRow(state_.selectedPosition()));
    }
}

void TableWidget::selectFirst() {
    ensureLayout();
    if (state_.selectFirst().accepted) {
        scroll_->setScrollY(layout_.scrollToRevealRow(state_.selectedPosition()));
    }
}

void TableWidget::selectLast() {
    ensureLayout();
    if (state_.selectLast().accepted) {
        scroll_->setScrollY(layout_.scrollToRevealRow(state_.selectedPosition()));
    }
}

// ---------------------------------------------------------------- 指针与每帧驱动

CursorShape TableWidget::cursorAt(const Vec2d& worldPoint) const noexcept {
    if (!hitTest(worldPoint)) {
        return CursorShape::Default;
    }
    bool consumedByChild = false;
    const CursorShape childCursor = childCursorAt(worldPoint, &consumedByChild);
    if (consumedByChild || childCursor != CursorShape::Default) {
        return childCursor;
    }
    const Vec2d local = localPoint(worldPoint);
    if (local.y >= style_.metrics.headerHeight) {
        return CursorShape::Default;
    }
    const int column = core::columnAtX(layout_, local.x + scroll_->scrollX());
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
        sceneDirty_ = true;
    }

    if (widthsDirty_ || sceneDirty_) {
        rebuild();
    }

    hoverPos_ = -1;
    // 若指针落在子组件（如滚动条滑块/轨道）上并被其 stopPointerPropagation 消费，
    // 基类三阶段路由会自动将 TableWidget::pointerInside() 置为 false，此处无需任何特判！
    if (pointerInside()) {
        const Vec2d local = localPoint(pointerPosition());
        if (local.y >= style_.metrics.headerHeight) {
            hoverPos_ = core::rowAtY(layout_, local.y - style_.metrics.headerHeight);
        }
    }

    if (hoverPos_ != lastHover_ || state_.selectedPosition() != lastSelected_) {
        syncHighlights();
    }
}

void TableWidget::onPress() {
    pressPoint_ = localPoint(pointerPosition());
}

void TableWidget::onRelease(bool inside) {
    if (!inside) {
        return;
    }
    const Vec2d local = localPoint(pointerPosition());
    const double headerHeight = style_.metrics.headerHeight;

    const double dx = local.x - pressPoint_.x;
    const double dy = local.y - pressPoint_.y;
    if (dx * dx + dy * dy > 36.0) {
        return;
    }

    if (local.y < headerHeight) {
        const int column = core::columnAtX(layout_, local.x + scroll_->scrollX());
        if (column >= 0 && static_cast<std::size_t>(column) < data_.columns.size()) {
            state_.cycleSort(data_.columns[static_cast<std::size_t>(column)].id);
            sceneDirty_ = true;
            rebuild();
            if (state_.selectedPosition() >= 0) {
                scroll_->setScrollY(layout_.scrollToRevealRow(state_.selectedPosition()));
            }
        }
        return;
    }

    const int position = core::rowAtY(layout_, local.y - headerHeight);
    if (position >= 0) {
        state_.selectPosition(position);
    }
}

// ---------------------------------------------------------------- 描述树对账与滚动联动

void TableWidget::syncHighlights() {
    lastHover_ = hoverPos_;
    lastSelected_ = state_.selectedPosition();
    if (bodyGroup_ != nullptr) {
        core::updateTableHighlights(*bodyGroup_, layout_, frame().size(), style_, hoverPos_,
                                    lastSelected_);
    }
}

void TableWidget::onScrollChanged(Vec2d offset) {
    if (widthsDirty_ || sceneDirty_) {
        return;
    }

    // 1. 横向滚动：O(1) 同步表头列平移
    if (offset.x != lastScroll_.x && chromeGroup_ != nullptr) {
        core::updateTableHeaderScroll(*chromeGroup_, offset.x);
    }

    // 2. 纵向滚动：重算可见行区间 [firstRow, lastRow)；
    //    仅当跨过行边界时才增量对账 bodyGroup_（未跨行时仅靠 ScrollWidget::content 平移即完成渲染）
    if (offset.y != lastScroll_.y) {
        const Vec2d canvasSize = frame().size();
        const double headerHeight = style_.metrics.headerHeight;

        core::TableLayoutInput input;
        input.columnWidths = widths_;
        input.rowCount = static_cast<int>(state_.rowCount());
        input.viewWidth = canvasSize.x;
        input.viewHeight = std::max(0.0, canvasSize.y - headerHeight);
        input.scrollOffset = offset.y;
        input.metrics = style_.metrics;
        layout_ = core::computeTableLayout(input);

        if (layout_.firstRow != lastFirstRow_ || layout_.lastRow != lastLastRow_) {
            if (auto bodyDesc = core::buildTableBody(data_, state_, layout_, canvasSize, style_,
                                                     hoverPos_)) {
                (void)core::reconcileScene(*bodyGroup_, *bodyDesc);
            }
            lastFirstRow_ = layout_.firstRow;
            lastLastRow_ = layout_.lastRow;
        }
    }

    lastScroll_ = offset;
}

void TableWidget::rebuild() {
    const Vec2d canvasSize = frame().size();
    const double width = std::max(0.0, canvasSize.x);
    const double height = std::max(0.0, canvasSize.y);
    const double headerHeight = style_.metrics.headerHeight;
    const double bodyHeight = std::max(0.0, height - headerHeight);

    if (widthsDirty_ || widths_.size() != data_.columns.size()) {
        widths_ = core::measureTableColumns(data_, width, style_.headerFontSize,
                                            style_.cellFontSize, style_.metrics.paddingX,
                                            measure_);
    }

    // 先算一次列总宽与完整内容高度，驱动 ScrollWidget 更新视口与夹紧后的 scrollOffset
    double totalWidth = 0.0;
    for (const double w : widths_) {
        totalWidth += std::max(0.0, w);
    }
    const double contentHeight =
        static_cast<double>(state_.rowCount()) * std::max(0.0, style_.metrics.rowHeight);

    scroll_->setTranslation({0.0, headerHeight});
    scroll_->setFrame({0.0, 0.0, width, bodyHeight});
    scroll_->setContentSize({totalWidth, contentHeight});
    // 触发 ScrollWidget 同步夹紧后的偏移（若行数减少导致旧 scrollY 越界，此处会立即夹紧）
    scroll_->setScrollOffset(scroll_->scrollOffset());

    core::TableLayoutInput input;
    input.columnWidths = widths_;
    input.rowCount = static_cast<int>(state_.rowCount());
    input.viewWidth = width;
    input.viewHeight = bodyHeight;
    input.scrollOffset = scroll_->scrollY();
    input.metrics = style_.metrics;
    layout_ = core::computeTableLayout(input);

    widthsDirty_ = false;
    sceneDirty_ = false;
    lastScroll_ = scroll_->scrollOffset();
    lastFirstRow_ = layout_.firstRow;
    lastLastRow_ = layout_.lastRow;
    lastHover_ = hoverPos_;
    lastSelected_ = state_.selectedPosition();

    if (auto chromeDesc =
            core::buildTableChrome(data_, state_, layout_, canvasSize, style_, lastScroll_.x)) {
        (void)core::reconcileScene(*chromeGroup_, *chromeDesc);
    }
    if (auto bodyDesc =
            core::buildTableBody(data_, state_, layout_, canvasSize, style_, hoverPos_)) {
        (void)core::reconcileScene(*bodyGroup_, *bodyDesc);
    }

    borderNode_->setFrame({0.0, 0.0, width, height});
    core::SceneStyle borderStyle;
    borderStyle.stroke = style_.border;
    borderStyle.strokeWidth = 1.0;
    borderStyle.cornerRadius = style_.radius;
    borderNode_->setStyle(borderStyle);
}

}  // namespace kiriyama::scene
