#pragma once

// 表格交互组件（scene 层，对齐 PieChart / SparkBar）：
//   · 组件自身持有 TableData（值语义 / 移动语义接管）与视图交互状态（排序、选中、hover）；
//   · 滚动交互（滚轮、横/纵滚动槽点击、滑块 Drag 拖拽、进度与边界夹紧）统一交由内部挂载的
//     ScrollWidget 子组件托管，彻底复用通用滚动容器语义；
//   · 正文行挂载在 ScrollWidget::content() 中，采用未扣滚动的逻辑内容坐标 y = position * rowHeight：
//     同一可见行窗口 [firstRow, lastRow) 内的微滚动仅靠 ScrollWidget 平移矩阵 O(1) 完成，
//     跨行边界时仅增量对账新进/移出的单行，已有行坐标恒定零改动。

#include <cstdint>
#include <string>
#include <vector>

#include "kiriyama/core/Layout.h"
#include "kiriyama/core/SceneBuilder.h"
#include "kiriyama/core/Table.h"
#include "kiriyama/core/TableLayout.h"
#include "kiriyama/scene/ScrollWidget.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

using TableStyle = core::TableStyle;

class TableWidget final : public Widget {
public:
    explicit TableWidget(std::string key = "table");

    // ------------------------------------------------------------ 配置

    void setStyle(const TableStyle& style);
    [[nodiscard]] const TableStyle& style() const noexcept { return style_; }

    /// 列宽测量用（表头 / 单元格文本宽度）。空 = core 估算。
    void setTextMeasure(core::TextMeasureFn measure) { measure_ = std::move(measure); }

    /// 访问内部托管正文滚动的 ScrollWidget（可定制滚动条样式、溢出策略或监听进度）。
    [[nodiscard]] ScrollWidget& scrollWidget() noexcept { return *scroll_; }
    [[nodiscard]] const ScrollWidget& scrollWidget() const noexcept { return *scroll_; }

    // ------------------------------------------------------------ 数据

    /// 组件自己持有数据（值语义 / 移动语义接管）：调用方传右值时零拷贝移入。
    void setData(core::TableData data);
    [[nodiscard]] const core::TableData& data() const noexcept { return data_; }

    [[nodiscard]] core::TableState& state() noexcept { return state_; }
    [[nodiscard]] const core::TableState& state() const noexcept { return state_; }

    // ------------------------------------------------------------ 滚动 / 选中（委托给 ScrollWidget 与 TableState）

    void scrollBy(double delta);
    void scrollTo(double offset);
    [[nodiscard]] double scrollOffset() const noexcept { return scroll_->scrollY(); }
    [[nodiscard]] core::Vec2d scrollOffset2D() const noexcept { return scroll_->scrollOffset(); }

    void moveSelection(int delta);
    void selectFirst();
    void selectLast();

    // ------------------------------------------------------------ 查询

    [[nodiscard]] core::CursorShape cursorAt(const core::Vec2d& worldPoint) const noexcept override;
    [[nodiscard]] int hoveredPosition() const noexcept { return hoverPos_; }
    [[nodiscard]] const core::TableLayout& layout() const noexcept { return layout_; }

    [[nodiscard]] std::string formatCell(const core::TableCell& cell,
                                         const core::TableColumn& column) const;

protected:
    void onUpdate(const FrameContext& ctx) override;
    void onPress() override;
    void onRelease(bool inside) override;

private:
    void ensureLayout();
    void rebuild();
    void onScrollChanged(core::Vec2d offset);
    void syncHighlights();
    [[nodiscard]] core::Vec2d localPoint(const core::Vec2d& worldPoint) const noexcept;

    TableStyle style_{};
    core::TextMeasureFn measure_{};

    core::TableState state_{};
    core::TableData data_{};
    std::uint64_t dataVersion_{0};
    bool widthsDirty_{true};
    bool sceneDirty_{true};

    std::vector<double> widths_{};
    core::TableLayout layout_{};
    core::Vec2d lastSize_{};

    int hoverPos_{-1};
    core::Vec2d pressPoint_{};

    core::SceneNode* chromeGroup_{nullptr};
    ScrollWidget* scroll_{nullptr};
    core::SceneNode* bodyGroup_{nullptr};
    core::SceneNode* borderNode_{nullptr};

    core::Vec2d lastScroll_{};
    int lastFirstRow_{0};
    int lastLastRow_{0};
    int lastHover_{-1};
    int lastSelected_{-1};
};

}  // namespace kiriyama::scene
