// Table 组件演示：模型写在 web/table.html 的 <script> 里，由 JS 编码后送进 wasm。
//
// 为什么这么做：
//   表格组件本身不持有数据（见 core/Table.h 的三条约定），数据归调用方。
//   这个 demo 的"调用方"就是页面 —— 模型、列定义、格式化串全在 HTML 里改，
//   不用重新编译就能换一份数据看效果。
//
// 传输格式（JS 侧 encodeModel 生成，这里 parseModel 解析）：
//   行分隔 '\n'，字段分隔 U+001F（单元分隔符，正常文本里不会出现，省掉转义）
//   C␟id␟title␟type␟width␟sortable␟format
//   R␟key␟cell1␟cell2␟...
//
// 组件这边只做三件事：把指针喂给 TableWidget、让它自己画、把排序状态显示在页脚。

#include "kiriyama/render/AppMain.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/Table.h"
#include "kiriyama/render/Font.h"
#include "kiriyama/render/Fonts.generated.h"
#include "kiriyama/render/TableWidget.h"
#include "kiriyama/render/TtfFont.h"
#include "kiriyama/render/SceneHost.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define KIRIYAMA_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define KIRIYAMA_EXPORT extern "C"
#endif

namespace {

using kiriyama::core::CellType;
using kiriyama::core::Color;
using kiriyama::core::RectD;
using kiriyama::core::TableData;
using kiriyama::core::TableCell;
using kiriyama::core::TableColumn;
using kiriyama::core::TableRow;
using kiriyama::render::App;
using kiriyama::render::AppConfig;
using kiriyama::render::Font;
using kiriyama::render::FontFace;
using kiriyama::render::FontLibrary;
using kiriyama::render::TableStyle;
using kiriyama::render::TableWidget;
using kiriyama::render::frameContext;

// ---------------------------------------------------------------- 配色

constexpr Color kBg = Color::Hex(0xF7F7F5);
constexpr Color kInk = Color::Hex(0x2C2C2A);
constexpr Color kMuted = Color::Hex(0x8A8A84);

// ---------------------------------------------------------------- 布局

constexpr double kContentX = 48.0;
constexpr double kContentWidth = 864.0;
constexpr double kTableY = 94.0;
constexpr double kTableHeight = 378.0;

// ---------------------------------------------------------------- 字体

std::shared_ptr<const FontFace> gBodyFace;

[[nodiscard]] Font body(double pixelSize) {
    return Font(gBodyFace ? gBodyFace : Font::builtinFace(), pixelSize);
}

// ---------------------------------------------------------------- JS -> C++ 模型

/// 字段分隔符：U+001F。JS 侧用 '\u001f' 拼接。
constexpr char kUnitSeparator = '\x1f';

[[nodiscard]] std::vector<std::string_view> splitFields(std::string_view line, char separator) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = line.find(separator, start);
        if (end == std::string_view::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, end - start));
        start = end + 1;
    }
}

[[nodiscard]] CellType parseCellType(std::string_view text) {
    if (text == "number") {
        return CellType::Number;
    }
    if (text == "int" || text == "integer") {
        return CellType::Integer;
    }
    if (text == "bool") {
        return CellType::Bool;
    }
    if (text == "date") {
        return CellType::Date;
    }
    return CellType::Text;
}

/// 按列声明的类型解析。空串 = 缺失，不是"空字符串"——
/// 表格里两者渲染结果一样，但排序时缺失永远排在一端。
[[nodiscard]] TableCell parseCell(std::string_view raw, CellType type) {
    if (raw.empty()) {
        return TableCell::Missing();
    }
    switch (type) {
        case CellType::Number:
        case CellType::Date: {
            const std::string buffer(raw);
            char* end = nullptr;
            const double value = std::strtod(buffer.c_str(), &end);
            if (end == buffer.c_str()) {
                return TableCell::Missing();
            }
            return TableCell{value};
        }
        case CellType::Integer: {
            const std::string buffer(raw);
            char* end = nullptr;
            const long long value = std::strtoll(buffer.c_str(), &end, 10);
            if (end == buffer.c_str()) {
                return TableCell::Missing();
            }
            return TableCell{static_cast<std::int64_t>(value)};
        }
        case CellType::Bool:
            return TableCell{raw == "1" || raw == "true" || raw == "yes"};
        case CellType::Text:
            return TableCell{std::string(raw)};
    }
    return TableCell::Missing();
}

[[nodiscard]] TableData parseModel(std::string_view text) {
    TableData data;

    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        const std::string_view line =
            newline == std::string_view::npos ? text : text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> fields = splitFields(line, kUnitSeparator);
        if (fields.empty()) {
            continue;
        }

        if (fields[0] == "C" && fields.size() >= 7) {
            TableColumn column;
            column.id = std::string(fields[1]);
            column.title = std::string(fields[2]);
            column.type = parseCellType(fields[3]);
            column.width = std::atof(std::string(fields[4]).c_str());
            column.sortable = fields[5] != "0";
            column.format = std::string(fields[6]);
            if (column.valid()) {
                data.columns.push_back(std::move(column));
            }
        } else if (fields[0] == "R" && fields.size() >= 2) {
            TableRow row;
            row.key = std::string(fields[1]);
            row.cells.reserve(fields.size() - 2);
            for (std::size_t i = 2; i < fields.size(); ++i) {
                const std::size_t columnIndex = i - 2;
                const CellType type = columnIndex < data.columns.size()
                                          ? data.columns[columnIndex].type
                                          : CellType::Text;
                row.cells.push_back(parseCell(fields[i], type));
            }
            data.rows.push_back(std::move(row));
        }
    }

    return data;
}

// ---------------------------------------------------------------- 收件箱

struct IncomingModel {
    TableData data;
    std::uint64_t version{0};
    bool fresh{false};
};

IncomingModel gIncoming;

}  // namespace

/// 页面调用的唯一入口：Module.ccall('kiriyama_table_load', null, ['string'], [text])。
/// 只做解析和登记，真正的替换发生在下一帧的 onFrame 里（避免在 JS 调用栈上动场景树）。
KIRIYAMA_EXPORT void kiriyama_table_load(const char* text) {
    if (text == nullptr) {
        return;
    }
    gIncoming.data = parseModel(text);
    ++gIncoming.version;
    gIncoming.fresh = true;
}

namespace {

// ---------------------------------------------------------------- 兜底数据
// 原生构建，或者 JS 还没把模型送进来时用它，保证页面不是一片空白。

[[nodiscard]] TableColumn makeColumn(std::string id, std::string title, CellType type,
                                     std::string format = {}) {
    TableColumn column;
    column.id = std::move(id);
    column.title = std::move(title);
    column.type = type;
    column.format = std::move(format);
    return column;
}

[[nodiscard]] TableData fallbackModel() {
    TableData data;
    data.columns.push_back(makeColumn("code", "CODE", CellType::Text));
    data.columns.push_back(makeColumn("name", "NAME", CellType::Text));
    data.columns.push_back(makeColumn("last", "LAST", CellType::Number, "%.2f"));
    data.columns.push_back(makeColumn("chg", "CHG", CellType::Number, "%+.2f%%"));
    data.columns.push_back(makeColumn("sector", "SECTOR", CellType::Text));

    auto add = [&data](const char* code, const char* name, double last, double change,
                       const char* sector) {
        TableRow row;
        row.key = code;
        row.cells = {TableCell{std::string(code)}, TableCell{std::string(name)},
                     TableCell{last}, TableCell{change}, TableCell{std::string(sector)}};
        data.rows.push_back(std::move(row));
    };
    add("AAPL", "Apple", 227.52, 1.24, "Technology");
    add("MSFT", "Microsoft", 416.32, -0.42, "Technology");
    add("XOM", "ExxonMobil", 118.42, 1.02, "Energy");
    add("JPM", "JPMorgan", 214.75, 0.88, "Financials");
    add("BA", "Boeing", 152.30, -2.47, "Industrials");
    return data;
}

// ---------------------------------------------------------------- Demo

class TableDemo final : public kiriyama::render::AppDelegate {
public:
    AppConfig config() const override {
        AppConfig config;
        config.title = "kiriyama/canvas - Table";
        config.windowWidth = 960;
        config.windowHeight = 620;
        config.logicalWidth = 960;
        config.logicalHeight = 540;
        config.clearColor = kBg;
        return config;
    }

    bool onInit(App& app) override {
        if (!kiriyama::render::embedded::kBodyFont.empty()) {
            gBodyFace = FontLibrary::shared().load(kiriyama::render::embedded::kBodyFont);
        }
        if (gBodyFace) {
            app.text().setDefaultFace(gBodyFace);
        }

        table_ = std::make_unique<TableWidget>("table");
        table_->setFace(gBodyFace ? gBodyFace : Font::builtinFace());
        table_->setStyle(makeStyle());
        table_->setFrame(RectD{kContentX, kTableY, kContentWidth, kTableHeight});

        // 没有 JS 模型时先给一份兜底数据，第一帧再被真正的模型换掉。
        data_ = fallbackModel();
        table_->setData(&data_, 0);
        return true;
    }

    void onEvent(App& app, const SDL_Event& event) override {
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            if (table_ != nullptr && table_->hitTest(app.pointer())) {
                table_->scrollBy(-static_cast<double>(event.wheel.y) * 36.0);
            }
            return;
        }
        if (event.type != SDL_EVENT_KEY_DOWN || table_ == nullptr) {
            return;
        }
        switch (event.key.key) {
            case SDLK_DOWN:
                table_->moveSelection(1);
                break;
            case SDLK_UP:
                table_->moveSelection(-1);
                break;
            case SDLK_PAGEDOWN:
                table_->moveSelection(9);
                break;
            case SDLK_PAGEUP:
                table_->moveSelection(-9);
                break;
            case SDLK_HOME:
                table_->selectFirst();
                break;
            case SDLK_END:
                table_->selectLast();
                break;
            default:
                break;
        }
    }

    void onFrame(App& app) override {
        adoptIncomingModel();

        // 指针账本（hover/pressed/clicked）由 Widget 记账，组件只需要被 tick 一次。
        table_->tickTree(frameContext(app));
        table_->draw(app.painter(), app.text());

        // 每帧归位：指针移开后要能变回箭头。SDL 只在 setCursor 时改形状。
        app.setCursor(table_->cursorAt(app.pointer()));

        drawHeader(app);
        drawFooter(app);
    }

private:
    [[nodiscard]] static TableStyle makeStyle() {
        TableStyle style;
        style.headerFontSize = 10.5;
        style.cellFontSize = 12.0;
        style.metrics.headerHeight = 36.0;
        style.metrics.rowHeight = 31.0;
        style.metrics.paddingX = 14.0;
        return style;
    }

    void adoptIncomingModel() {
        if (!gIncoming.fresh) {
            return;
        }
        data_ = std::move(gIncoming.data);
        gIncoming.data = {};
        gIncoming.fresh = false;
        table_->setData(&data_, gIncoming.version);
    }

    void drawHeader(App& app) {
        auto& text = app.text();
        text.drawText("Table component", body(24.0), {kContentX, 24.0}, {}, kInk);

        const std::string subtitle =
            std::string("core::TableState  ·  core::computeTableLayout  ·  model in table.html  ·  ") +
            (gBodyFace ? std::string(gBodyFace->family()) : std::string("builtin bitmap"));
        text.drawText(subtitle, body(11.0), {kContentX, 58.0}, {}, kMuted);
    }

    void drawFooter(App& app) {
        auto& text = app.text();

        const auto& state = table_->state();
        std::string sortText = "unsorted";
        if (state.sorted()) {
            const auto sort = state.sort();
            std::string title = sort.columnId;
            if (const TableColumn* column = data_.findColumn(sort.columnId)) {
                title = column->title;
            }
            sortText = "sorted by " + title + (sort.dir == kiriyama::core::SortDir::Asc ? " asc"
                                                                                       : " desc");
        }

        std::string status = std::to_string(data_.rowCount()) + " rows  ·  " + sortText;
        if (state.selectedPosition() >= 0) {
            status += "  ·  row " + std::to_string(state.selectedPosition() + 1);
        }
        text.drawText(status, body(11.0), {kContentX, kTableY + kTableHeight + 14.0}, {}, kMuted);

        kiriyama::render::TextLayoutOptions options;
        options.align = kiriyama::core::TextAlign::Right;
        options.maxWidth = kContentWidth;
        text.drawText("click a sortable header  ·  wheel to scroll  ·  arrow keys to select",
                      body(11.0), {kContentX, kTableY + kTableHeight + 14.0}, options, kMuted);
    }

    TableData data_{};
    std::unique_ptr<TableWidget> table_{};
};

}  // namespace

KIRIYAMA_MAIN(TableDemo)
