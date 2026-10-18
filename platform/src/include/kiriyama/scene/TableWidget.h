#pragma once

// 表格组件：Widget（指针账本）+ core::TableState（排序 / 选中）+ core::TableLayout（几何）。
//
// **组件持有 model**（setData 拷贝进来，值语义），并持有视图状态
// （排序键、选中行、滚动量、hover 行）——这些都是纯 UI 的、跨帧必须记住的东西。
//
// 渲染走描述树：onUpdate 里把"当前帧应该长什么样"整体描述出来
// （buildDescription：卡片 / 表头 / 可见行 / 单元格文本 / 排序三角），
// reconcileScene 就地对账进固定的 plot 子组——滚动一行只写变化的几个节点，
// 没有任何"全量拆除重建"。裁剪用节点的 clipRect（滚动视口），渲染层落成平台裁剪。
//
// 为什么在 scene/ 而不是 render/：
//   自动列宽的字体度量走注入的 TextMeasureFn（宿主接真实字体，测试用估算），
//   绘制走渲染层的通用图元遍历 + TextRenderer——本组件一行 SDL 都不碰。
//
// 用法：
//   scene::TableWidget table("t");
//   table.setTextMeasure(&textRenderer 的度量回调);
//   table.setFrame(RectD{48, 92, 864, 380});
//   table.setData(data);                       // 值语义，拷贝进组件
//   // 每帧
//   table.tickTree(frameContext(app));         // 指针状态 + hover 行 + 描述树对账
//   drawSceneGeometry(painter, table);         // 几何
//   textRenderer.drawScene(table);             // 文本
//   app.setCursor(table.cursorAt(app.pointer()));

#include <cstdint>
#include <string>
#include <vector>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/Layout.h"
#include "kiriyama/core/Table.h"
#include "kiriyama/core/TableLayout.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

struct TableStyle {
    core::Color background{core::Color::Hex(0xFFFFFF)};
    core::Color headerBackground{core::Color::Hex(0xF4F3F0)};
    core::Color headerText{core::Color::Hex(0x6E6E68)};
    core::Color headerActiveText{core::Color::Hex(0x2C2C2A)};
    core::Color rowText{core::Color::Hex(0x2C2C2A)};
    core::Color mutedText{core::Color::Hex(0x9A9A94)};
    core::Color grid{core::Color::Hex(0xEDEBE5)};
    core::Color border{core::Color::Hex(0xDEDCD4)};
    core::Color hoverRow{core::Color::Hex(0xF7F7F4)};
    core::Color selectedRow{core::Color::Hex(0xE8F1FB)};
    core::Color accent{core::Color::Hex(0x3E6DB5)};
    /// 涨=红 / 跌=绿（国内行情惯例）。只作用于"格式化串里带 +"的列。
    core::Color positive{core::Color::Hex(0xC0392B)};
    core::Color negative{core::Color::Hex(0x2E7D4F)};

    double radius{10.0};
    double headerFontSize{11.0};
    double cellFontSize{12.5};
    core::TableMetrics metrics{};
};

class TableWidget final : public Widget {
public:
    explicit TableWidget(std::string key = "table");

    // ------------------------------------------------------------ 配置

    void setStyle(const TableStyle& style);
    [[nodiscard]] const TableStyle& style() const noexcept { return style_; }

    /// 列宽测量用（表头 / 单元格文本宽度）。空 = core 估算。
    void setTextMeasure(core::TextMeasureFn measure) { measure_ = std::move(measure); }

    // ------------------------------------------------------------ 数据

    /// 拷贝进组件（值语义）：之后调用方改自己的副本互不影响。
    /// 每次调用都重新量列宽、重置排序与选中。
    void setData(const core::TableData& data);
    [[nodiscard]] const core::TableData& data() const noexcept { return data_; }

    [[nodiscard]] core::TableState& state() noexcept { return state_; }
    [[nodiscard]] const core::TableState& state() const noexcept { return state_; }

    // ------------------------------------------------------------ 滚动 / 选中

    void scrollBy(double delta);
    void scrollTo(double offset);
    [[nodiscard]] double scrollOffset() const noexcept { return scroll_; }

    /// 按显示位置上下移动选中行，并自动滚到可见。
    void moveSelection(int delta);
    void selectFirst();
    void selectLast();

    // ------------------------------------------------------------ 查询

    /// 指针处的光标形状：只有**可排序**的表头是手型，其余 default。
    [[nodiscard]] core::CursorShape cursorAt(const core::Vec2d& worldPoint) const noexcept;

    [[nodiscard]] int hoveredPosition() const noexcept { return hoverPos_; }
    [[nodiscard]] const core::TableLayout& layout() const noexcept { return layout_; }

    /// 单元格的显示串。排序用的是 TableCell，显示串到这里才生成（数字列走 format）。
    [[nodiscard]] std::string formatCell(const core::TableCell& cell,
                                         const core::TableColumn& column) const;

protected:
    void onUpdate(const FrameContext& ctx) override;
    void onPress() override;
    void onRelease(bool inside) override;

private:
    void relayout();
    void remeasure();
    void markSceneDirty() noexcept { sceneDirty_ = true; }
    void rebuild();
    [[nodiscard]] core::SceneNode* plotGroup();
    void buildDescription(core::SceneNode& plot);
    void scrollRowIntoView(int position);

    [[nodiscard]] core::Vec2d origin() const noexcept;
    /// 世界 → 组件本地（卡片左上角为原点）。
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

    double scroll_{0.0};
    int hoverPos_{-1};
    core::Vec2d pressPoint_{};

    core::SceneNode* plot_{nullptr};
    core::SceneNode* inner_{nullptr};  ///< 对账目标（plot 持平移，不参与对账）
    double lastScroll_{0.0};
    int lastHover_{-1};
    int lastSelected_{-1};
};

}  // namespace kiriyama::scene
