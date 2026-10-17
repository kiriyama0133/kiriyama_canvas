#pragma once

// 表格组件：Widget（指针账本）+ core::TableState（排序 / 选中）+ core::TableLayout（几何）。
//
// 它**不持有数据**：TableData 归调用方，这里只留 const 指针 + 调用方给的 dataVersion。
// 组件持有的是视图状态（排序键、选中行、滚动量、hover 行）——这些都是纯 UI 的、
// 跨帧必须记住的东西，放在调用方那里，第二个表格就要复制第二份。
//
// 为什么在 render/ 而不是 scene/：
//   自动列宽要量字体（Font::measure），绘制要 Painter，两者都在渲染层。
//   scene/ 只依赖 core，装不下这两样。行为和交互逻辑仍然是 Widget 那一套。
//
// 用法：
//   render::TableWidget table("t");
//   table.setFace(face);                 // 度量与绘制共用一支字体
//   table.setFrame(RectD{48, 92, 864, 380});
//   table.setData(&data, version);
//   // 每帧
//   table.tickTree(frameContext(app));   // 指针状态 + hover 行
//   table.draw(app.painter(), app.text());
//   app.setCursor(table.cursorAt(app.pointer()));

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/Table.h"
#include "kiriyama/core/TableLayout.h"
#include "kiriyama/render/Font.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::render {

class Painter;
class SdlTextRenderer;
class FontFace;

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

class TableWidget final : public scene::Widget {
public:
    explicit TableWidget(std::string key = "table");

    // ------------------------------------------------------------ 配置

    void setStyle(const TableStyle& style);
    [[nodiscard]] const TableStyle& style() const noexcept { return style_; }

    void setFace(std::shared_ptr<const FontFace> face);
    [[nodiscard]] const std::shared_ptr<const FontFace>& face() const noexcept { return face_; }

    // ------------------------------------------------------------ 数据

    /// data 归调用方，必须活得比本组件久。version 变了才重新量列宽。
    void setData(const core::TableData* data, std::uint64_t version = 0);
    [[nodiscard]] const core::TableData* data() const noexcept { return data_; }

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

    // ------------------------------------------------------------ 绘制

    void draw(Painter& painter, SdlTextRenderer& text);

protected:
    void onUpdate(const scene::FrameContext& ctx) override;
    void onPress() override;
    void onRelease(bool inside) override;

private:
    void relayout();
    void remeasure();
    void applyStyle();
    void scrollRowIntoView(int position);

    [[nodiscard]] core::Vec2d origin() const noexcept;
    /// 世界 → 组件本地（卡片左上角为原点）。
    [[nodiscard]] core::Vec2d localPoint(const core::Vec2d& worldPoint) const noexcept;

    TableStyle style_{};
    std::shared_ptr<const FontFace> face_{};
    Font headerFont_{};
    Font cellFont_{};

    core::TableState state_{};
    const core::TableData* data_{nullptr};
    std::uint64_t dataVersion_{0};
    bool widthsDirty_{true};

    std::vector<double> widths_{};
    core::TableLayout layout_{};

    double scroll_{0.0};
    int hoverPos_{-1};
    core::Vec2d pressPoint_{};
};

}  // namespace kiriyama::render
