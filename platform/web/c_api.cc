#include "kiriyama/web/c_api.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/Table.h"
#include "kiriyama/math/MathUtils.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define KIRIYAMA_KEEP EMSCRIPTEN_KEEPALIVE
#else
#define KIRIYAMA_KEEP
#endif

// 统一 C API 的实现。约定：
//   1. kiriyama_* 的定义一律在**全局作用域** + KIRIYAMA_KEEP。
//      塞进 namespace 会和 c_api.h 的 extern "C" 声明成为两个实体，
//      符号被 C++ 修饰 → wasm 导出表查不到 → ccall 报 "not a function"。
//   2. 所有函数只登记状态（pending），宿主每帧 pump*() 统一消费。
//   3. 组件自己的 namespace 是 kiriyama::web，内部状态放文件级匿名 namespace。
//
// 状态跟着**实例**走，不是文件级单例：gSlots 里一个槽就是一个组件实例，
// 句柄 = 下标 + 1。两个饼图各存各的 slices / style / hover，互不影响。

using kiriyama::core::Color;
using kiriyama::core::PieChartData;
using kiriyama::core::PieLayoutOptions;
using kiriyama::core::PieSlice;
using kiriyama::render::TooltipHost;
using kiriyama::render::TooltipStyle;
using kiriyama::scene::PieChart;
using kiriyama::scene::SparkBar;

// 下面 SparkStackBar / Pie 的状态统一用 core:: 前缀（别名，避免和上面 using 的名字打架）。
namespace core = kiriyama::core;

using kiriyama::web::Component;

namespace {

/// 字段分隔符：U+001F，与 web/entry.js 同一个约定。
constexpr char kUnitSeparator = '\x1f';

// ---------------------------------------------------------------- 每实例状态

struct PieState {
    /// pending 数据缓冲：pump 时拷贝（移动语义清空）进组件，组件自己持有 model。
    PieChartData data;

    /// begin/add/commit 的临时缓冲（提交前不动 data）。
    std::vector<PieSlice> buffered;
    bool collecting{false};

    core::PieStyle style{};
    PieLayoutOptions& layout{style.layout};
    double& explode{style.explodeDistance};
    bool dataDirty{false};
    bool styleDirty{false};

    /// 宿主每帧上报 / 查询。
    int hovered{-1};
    int sliceCount{0};
};

struct SparkState {
    core::SparkBarData data;
    core::SparkBarOptions options;
    std::vector<Color> colors;
    bool dirty{false};

    /// begin / add_series / begin_row / add_value / end_row / commit 的临时缓冲。
    std::vector<core::SparkBarSeries> seriesBuf;
    std::vector<std::string> tagsBuf;
    std::vector<std::vector<double>> valuesBuf;
    std::string rowTag;
    std::vector<double> rowValues;
    bool rowOpen{false};
};

struct TableState {
    core::TableData data;
    bool dirty{false};
};

struct LineState {
    core::Dataset data;
    kiriyama::scene::LineChartConfig config{};
    std::vector<Color> palette{};
    bool dataDirty{false};
    bool configDirty{false};

    /// begin / add_* / commit 的临时缓冲。
    std::vector<std::string> xLabelsBuf{};
    std::vector<std::string> yLabelsBuf{};
    std::vector<core::Series> seriesBuf{};
    std::vector<Color> colorsBuf{};
    core::Series currentSeries{};
    bool seriesOpen{false};

    int hoveredSeries{-1};
    int hoveredPoint{-1};
};

/// 每个实例一份气泡配置（两个饼图可以一个贴上一个贴右）。
struct TooltipState {
    double arrowWidth{0.0};
    double fontSize{0.0};  ///< <= 0 表示用 TooltipStyle 默认
    int side{-1};          ///< < 0 表示不改（跟随鼠标时默认贴右侧）
    bool followPointer{true};
    bool dirty{false};
};

struct Slot {
    int handle{0};
    Component type{Component::Pie};
    bool visible{false};
    core::RectD frame{};

    PieState pie;
    SparkState spark;
    TableState table;
    LineState line;
    TooltipState tip;
};

/// 句柄 → 实例。句柄**单调递增、永不复用**：复用会让宿主以为"这个句柄我已有
/// 节点了"，而那个节点可能是上一个类型（清空后挂 sparkbar 又拿回句柄 1，
/// 于是继续画上一个饼图）。宁可留一个空洞，也不要让旧句柄指向新实例。
std::map<int, std::unique_ptr<Slot>> gSlots;
int gNextHandle{1};

/// 页面主题（token 解析来源）。描述树里没有颜色，切主题零重建。
core::ChartTheme gTheme = core::ChartTheme::Light();

[[nodiscard]] Slot* findSlot(int handle) {
    const auto it = gSlots.find(handle);
    return it == gSlots.end() ? nullptr : it->second.get();
}

/// 类型表里没有的返回 nullopt（create 失败 / show 忽略）。
[[nodiscard]] std::optional<Component> componentType(std::string_view type) {
    if (type == "pie") {
        return Component::Pie;
    }
    if (type == "sparkbar") {
        return Component::SparkBar;
    }
    if (type == "table") {
        return Component::Table;
    }
    if (type == "line") {
        return Component::Line;
    }
    return std::nullopt;
}

/// 颜色统一走这里：负值 = 透明，其余按 0xRRGGBB 取。
[[nodiscard]] Color colorFromRgb(double rgb) {
    return rgb < 0.0 ? Color::Rgba(0.0, 0.0, 0.0, 0.0)
                     : Color::Hex(static_cast<std::uint32_t>(rgb));
}

/// 空 label / 非有限值按缺失跳过（口径同数据清洗）。
void appendSlice(PieState& pie, std::string label, double value) {
    if (label.empty() || !std::isfinite(value)) {
        return;
    }
    pie.buffered.push_back({std::move(label), value});
}

// ---------------------------------------------------------------- 表格解析
// 数据来自页面（entry.js 的 encodeTable），这里只解析、登记、等宿主 pump。

/// 列定义必须 7 段（含行首标记），少一段就当坏行跳过。
constexpr std::size_t kColumnFieldCount = 7;

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

[[nodiscard]] core::CellType parseCellType(std::string_view text) {
    if (text == "number") {
        return core::CellType::Number;
    }
    if (text == "int" || text == "integer") {
        return core::CellType::Integer;
    }
    if (text == "bool") {
        return core::CellType::Bool;
    }
    if (text == "date") {
        return core::CellType::Date;
    }
    return core::CellType::Text;
}

/// 空串 = 缺失。表格里"缺失"和"空字符串"渲染一样，但排序时缺失固定排一端。
[[nodiscard]] core::TableCell parseCell(std::string_view raw, core::CellType type) {
    if (raw.empty()) {
        return core::TableCell::Missing();
    }
    switch (type) {
        case core::CellType::Number:
        case core::CellType::Date: {
            const std::string buffer(raw);
            char* end = nullptr;
            const double value = std::strtod(buffer.c_str(), &end);
            if (end == buffer.c_str()) {
                return core::TableCell::Missing();
            }
            return core::TableCell{value};
        }
        case core::CellType::Integer: {
            const std::string buffer(raw);
            char* end = nullptr;
            const long long value = std::strtoll(buffer.c_str(), &end, 10);
            if (end == buffer.c_str()) {
                return core::TableCell::Missing();
            }
            return core::TableCell{static_cast<std::int64_t>(value)};
        }
        case core::CellType::Bool:
            return core::TableCell{raw == "1" || raw == "true" || raw == "yes"};
        case core::CellType::Text:
            return core::TableCell{std::string(raw)};
    }
    return core::TableCell::Missing();
}

[[nodiscard]] core::TableData parseTableModel(std::string_view text) {
    core::TableData data;

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

        if (fields[0] == "C" && fields.size() >= kColumnFieldCount) {
            core::TableColumn column;
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
            core::TableRow row;
            row.key = std::string(fields[1]);
            row.cells.reserve(fields.size() - 2);
            for (std::size_t i = 2; i < fields.size(); ++i) {
                const std::size_t columnIndex = i - 2;
                const core::CellType type = columnIndex < data.columns.size()
                                                ? data.columns[columnIndex].type
                                                : core::CellType::Text;
                row.cells.push_back(parseCell(fields[i], type));
            }
            data.rows.push_back(std::move(row));
        }
    }

    return data;
}

}  // namespace

// ================================================================ 实例生命周期

KIRIYAMA_KEEP int kiriyama_create_component(const char* type) {
    const std::optional<Component> kind = componentType(type != nullptr ? type : "");
    if (!kind) {
        return 0;
    }
    const int handle = gNextHandle++;
    auto slot = std::make_unique<Slot>();
    slot->handle = handle;
    slot->type = *kind;
    gSlots.emplace(handle, std::move(slot));
    return handle;
}

KIRIYAMA_KEEP void kiriyama_destroy_component(int handle) {
    gSlots.erase(handle);
}

KIRIYAMA_KEEP void kiriyama_clear_components(void) {
    gSlots.clear();
}

KIRIYAMA_KEEP int kiriyama_component_count(void) {
    return static_cast<int>(gSlots.size());
}

// ================================================================ 主题

KIRIYAMA_KEEP void kiriyama_set_theme(const char* name) {
    const std::string_view requested = name != nullptr ? name : "";
    if (requested == "dark") {
        gTheme = core::ChartTheme::Dark();
    } else if (requested == "light") {
        gTheme = core::ChartTheme::Light();
    }
    // 其他值忽略：调用方拼写错误不该把页面切成一个未知状态。
}

// ================================================================ 挂载

KIRIYAMA_KEEP void kiriyama_show_component(int handle, double x, double y, double width,
                                           double height) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || !(width > 0.0) || !(height > 0.0)) {
        return;
    }
    slot->visible = true;
    slot->frame = core::RectD{x, y, width, height};
}

KIRIYAMA_KEEP void kiriyama_hide_component(int handle) {
    Slot* slot = findSlot(handle);
    if (slot != nullptr) {
        slot->visible = false;
    }
}

KIRIYAMA_KEEP int kiriyama_component_visible(int handle) {
    const Slot* slot = findSlot(handle);
    return slot != nullptr && slot->visible ? 1 : 0;
}

// ================================================================ 饼图 · 数据

KIRIYAMA_KEEP void kiriyama_pie_begin_slices(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    slot->pie.buffered.clear();
    slot->pie.collecting = true;
}

KIRIYAMA_KEEP void kiriyama_pie_add_slice(int handle, const char* label, double value) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || label == nullptr) {
        return;
    }
    if (!slot->pie.collecting) {
        // 没 begin 就当隐式 begin：调用方少写一行也能用。
        slot->pie.buffered.clear();
        slot->pie.collecting = true;
    }
    appendSlice(slot->pie, label, value);
}

KIRIYAMA_KEEP void kiriyama_pie_commit(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    PieState& pie = slot->pie;
    pie.collecting = false;
    pie.data.slices = std::move(pie.buffered);
    pie.buffered.clear();
    pie.dataDirty = true;
}

KIRIYAMA_KEEP void kiriyama_pie_load(int handle, const char* text) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || text == nullptr) {
        return;
    }

    PieState& pie = slot->pie;
    pie.buffered.clear();
    pie.collecting = false;

    std::string_view rest(text);
    while (!rest.empty()) {
        const std::size_t newline = rest.find('\n');
        const std::string_view line =
            newline == std::string_view::npos ? rest : rest.substr(0, newline);
        rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
        if (line.empty()) {
            continue;
        }

        const std::size_t sep = line.find(kUnitSeparator);
        if (sep == std::string_view::npos) {
            continue;
        }
        const std::string_view label = line.substr(0, sep);
        const std::string_view valueText = line.substr(sep + 1);
        if (label.empty() || valueText.empty()) {
            continue;
        }
        appendSlice(pie, std::string(label), std::strtod(std::string(valueText).c_str(), nullptr));
    }

    pie.data.slices = std::move(pie.buffered);
    pie.buffered.clear();
    pie.dataDirty = true;
}

// ================================================================ 饼图 · 样式

KIRIYAMA_KEEP void kiriyama_pie_set_label_font_size(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.style.labelFontSize = pixels;
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_label_color(int handle, double rgb) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.style.labelColor = colorFromRgb(rgb);
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_show_labels(int handle, double enabled) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.style.showLabels = enabled != 0.0;
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_show_values(int handle, double enabled) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.style.showValues = enabled != 0.0;
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_gap_width(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.style.gapWidth = pixels;
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_gap_color(int handle, double rgb) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.style.gapColor = colorFromRgb(rgb);
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_explode(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.explode = pixels;
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_inner_radius(int handle, double ratio) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.layout.innerRadiusRatio = ratio;
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_start_angle(int handle, double degrees) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.layout.startAngle = degrees * (kiriyama::math::kPi / 180.0);
        slot->pie.styleDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_pie_set_label_offset(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.layout.labelOffset = pixels;
        slot->pie.styleDirty = true;
    }
}

// ================================================================ 饼图 · 查询

KIRIYAMA_KEEP int kiriyama_pie_slice_count(int handle) {
    const Slot* slot = findSlot(handle);
    return slot == nullptr ? 0 : slot->pie.sliceCount;
}

KIRIYAMA_KEEP int kiriyama_pie_hovered(int handle) {
    const Slot* slot = findSlot(handle);
    return slot == nullptr ? -1 : slot->pie.hovered;
}

// ================================================================ 通用 · 气泡

KIRIYAMA_KEEP void kiriyama_tooltip_set_arrow_width(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->tip.arrowWidth = pixels < 0.0 ? 0.0 : pixels;
        slot->tip.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_tooltip_set_font_size(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->tip.fontSize = pixels;
        slot->tip.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_tooltip_set_side(int handle, double side) {
    // 钳到 above..right；负数/越界按默认（不改）。
    if (Slot* slot = findSlot(handle)) {
        if (side >= 0.0 && side <= 3.0) {
            slot->tip.side = static_cast<int>(side);
            slot->tip.dirty = true;
        }
    }
}

KIRIYAMA_KEEP void kiriyama_tooltip_set_follow_pointer(int handle, double enabled) {
    if (Slot* slot = findSlot(handle)) {
        slot->tip.followPointer = enabled != 0.0;
        slot->tip.dirty = true;
    }
}

// ================================================================ SparkStackBar · 数据

KIRIYAMA_KEEP void kiriyama_sparkbar_begin(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    SparkState& spark = slot->spark;
    spark.seriesBuf.clear();
    spark.tagsBuf.clear();
    spark.valuesBuf.clear();
    spark.rowValues.clear();
    spark.rowTag.clear();
    spark.rowOpen = false;
}

KIRIYAMA_KEEP void kiriyama_sparkbar_add_series(int handle, const char* name, double rgb) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    core::SparkBarSeries series;
    series.name = name != nullptr ? name : "";
    series.color = colorFromRgb(rgb);
    slot->spark.seriesBuf.push_back(std::move(series));
}

KIRIYAMA_KEEP void kiriyama_sparkbar_begin_row(int handle, const char* tag) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    slot->spark.rowTag = tag != nullptr ? tag : "";
    slot->spark.rowValues.clear();
    slot->spark.rowOpen = true;
}

KIRIYAMA_KEEP void kiriyama_sparkbar_add_value(int handle, double value) {
    Slot* slot = findSlot(handle);
    if (slot != nullptr && slot->spark.rowOpen) {
        slot->spark.rowValues.push_back(value);
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_end_row(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || !slot->spark.rowOpen) {
        return;
    }
    SparkState& spark = slot->spark;
    spark.tagsBuf.push_back(std::move(spark.rowTag));
    spark.valuesBuf.push_back(std::move(spark.rowValues));
    spark.rowTag.clear();
    spark.rowValues.clear();
    spark.rowOpen = false;
}

KIRIYAMA_KEEP void kiriyama_sparkbar_commit(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    SparkState& spark = slot->spark;
    spark.data.series = std::move(spark.seriesBuf);
    spark.data.tags = std::move(spark.tagsBuf);
    spark.data.values = std::move(spark.valuesBuf);
    spark.seriesBuf.clear();
    spark.tagsBuf.clear();
    spark.valuesBuf.clear();

    // 颜色：series 给了就用，没给（透明）的退回默认色板。
    spark.colors.clear();
    spark.colors.reserve(spark.data.series.size());
    for (const core::SparkBarSeries& series : spark.data.series) {
        spark.colors.push_back(series.color);
    }

    spark.dirty = true;
}

// ================================================================ SparkStackBar · 样式

KIRIYAMA_KEEP void kiriyama_sparkbar_set_bar_width(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.barWidth = pixels < 0.0 ? 0.0 : pixels;
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_bar_gap(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.barGap = pixels < 0.0 ? 0.0 : pixels;
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_segment_gap(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.segmentGap = pixels < 0.0 ? 0.0 : pixels;
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_corner_radius(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.cornerRadius = pixels < 0.0 ? 0.0 : pixels;
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_hover_border_width(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.hoverBorderWidth = pixels < 0.0 ? 0.0 : pixels;
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_tag_font_size(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.tagFontSize = pixels;
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_tag_color(int handle, double rgb) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.tagColor = colorFromRgb(rgb);
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_show_tags(int handle, double enabled) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.showTags = enabled != 0.0;
        slot->spark.dirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_sparkbar_set_height_ratio(int handle, double ratio) {
    if (Slot* slot = findSlot(handle)) {
        slot->spark.options.heightRatio = ratio;
        slot->spark.dirty = true;
    }
}

// ================================================================ Table · 数据

KIRIYAMA_KEEP void kiriyama_table_load(int handle, const char* text) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || text == nullptr) {
        return;
    }
    slot->table.data = parseTableModel(text);
    slot->table.dirty = true;
}

// ================================================================ LineChart · 数据

KIRIYAMA_KEEP void kiriyama_line_begin(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    LineState& line = slot->line;
    line.xLabelsBuf.clear();
    line.yLabelsBuf.clear();
    line.seriesBuf.clear();
    line.colorsBuf.clear();
    line.currentSeries = {};
    line.seriesOpen = false;
}

KIRIYAMA_KEEP void kiriyama_line_add_x_label(int handle, const char* label) {
    if (Slot* slot = findSlot(handle); slot != nullptr && label != nullptr) {
        slot->line.xLabelsBuf.emplace_back(label);
    }
}

KIRIYAMA_KEEP void kiriyama_line_add_y_label(int handle, const char* label) {
    if (Slot* slot = findSlot(handle); slot != nullptr && label != nullptr) {
        slot->line.yLabelsBuf.emplace_back(label);
    }
}

KIRIYAMA_KEEP void kiriyama_line_begin_series(int handle, const char* name, double rgb) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    LineState& line = slot->line;
    line.currentSeries = {};
    line.currentSeries.name = name != nullptr ? name : "";
    line.colorsBuf.push_back(colorFromRgb(rgb));
    line.seriesOpen = true;
}

KIRIYAMA_KEEP void kiriyama_line_add_point(int handle, double x, double y) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || !slot->line.seriesOpen) {
        return;
    }
    if (std::isfinite(x) && std::isfinite(y)) {
        slot->line.currentSeries.addPoint(x, y);
    } else {
        slot->line.currentSeries.addNull(std::isfinite(x) ? x : 0.0);
    }
}

KIRIYAMA_KEEP void kiriyama_line_end_series(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || !slot->line.seriesOpen) {
        return;
    }
    LineState& line = slot->line;
    line.seriesBuf.push_back(std::move(line.currentSeries));
    line.currentSeries = {};
    line.seriesOpen = false;
}

KIRIYAMA_KEEP void kiriyama_line_commit(int handle) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    LineState& line = slot->line;
    if (line.seriesOpen) {
        line.seriesBuf.push_back(std::move(line.currentSeries));
        line.currentSeries = {};
        line.seriesOpen = false;
    }
    line.data.categories = line.xLabelsBuf;
    line.data.series = std::move(line.seriesBuf);
    line.config.layout.x.tickLabels = std::move(line.xLabelsBuf);
    line.config.layout.y.tickLabels = std::move(line.yLabelsBuf);

    bool hasCustomColor = false;
    for (const Color& c : line.colorsBuf) {
        if (c.visible()) {
            hasCustomColor = true;
            break;
        }
    }
    if (hasCustomColor) {
        const auto& defaults = core::defaultSeriesPalette();
        line.palette.clear();
        line.palette.reserve(line.colorsBuf.size());
        for (std::size_t i = 0; i < line.colorsBuf.size(); ++i) {
            if (line.colorsBuf[i].visible()) {
                line.palette.push_back(line.colorsBuf[i]);
            } else if (!defaults.empty()) {
                line.palette.push_back(defaults[i % defaults.size()]);
            }
        }
    } else {
        line.palette.clear();
    }

    line.xLabelsBuf.clear();
    line.yLabelsBuf.clear();
    line.seriesBuf.clear();
    line.colorsBuf.clear();
    line.dataDirty = true;
    line.configDirty = true;
}

// ================================================================ LineChart · 配置与样式

KIRIYAMA_KEEP void kiriyama_line_set_title(int handle, const char* title) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.title = title != nullptr ? title : "";
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_curve(int handle, double monotone) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.curve =
            monotone != 0.0 ? core::LineCurve::Monotone : core::LineCurve::Linear;
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_line_width(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.lineWidth = std::max(0.0, pixels);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_show_area(int handle, double enabled) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.showArea = enabled != 0.0;
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_area_opacity(int handle, double opacity) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.areaOpacity = kiriyama::math::saturate(opacity);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_show_grid(int handle, double enabled) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.showGrid = enabled != 0.0;
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_point_radius(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.pointStyle.radius = std::max(0.0, pixels);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_hover_point_radius(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.pointStyle.hoverRadius = std::max(0.0, pixels);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_halo_radius(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.pointStyle.haloRadius = std::max(0.0, pixels);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_hit_radius(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.pointStyle.hitRadius = std::max(0.0, pixels);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_x_title(int handle, const char* title) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.x.title = title != nullptr ? title : "";
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_x_font_size(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.x.labelFontSize = std::max(6.0, pixels);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_x_color(int handle, double rgb) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.x.labelColor = colorFromRgb(rgb);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_x_direction(int handle, double vertical) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.x.labelDirection =
            vertical != 0.0 ? core::TextDirection::Vertical : core::TextDirection::Horizontal;
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_x_angle(int handle, double degrees) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.x.labelAngle = degrees * (kiriyama::math::kPi / 180.0);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_y_title(int handle, const char* title) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.y.title = title != nullptr ? title : "";
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_y_font_size(int handle, double pixels) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.y.labelFontSize = std::max(6.0, pixels);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_y_color(int handle, double rgb) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.y.labelColor = colorFromRgb(rgb);
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_y_direction(int handle, double vertical) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.y.labelDirection =
            vertical != 0.0 ? core::TextDirection::Vertical : core::TextDirection::Horizontal;
        slot->line.configDirty = true;
    }
}

KIRIYAMA_KEEP void kiriyama_line_set_y_angle(int handle, double degrees) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.config.layout.y.labelAngle = degrees * (kiriyama::math::kPi / 180.0);
        slot->line.configDirty = true;
    }
}

// ================================================================ LineChart · 查询

KIRIYAMA_KEEP int kiriyama_line_hovered_series(int handle) {
    const Slot* slot = findSlot(handle);
    return slot == nullptr ? -1 : slot->line.hoveredSeries;
}

KIRIYAMA_KEEP int kiriyama_line_hovered_point(int handle) {
    const Slot* slot = findSlot(handle);
    return slot == nullptr ? -1 : slot->line.hoveredPoint;
}

// ================================================================ 宿主侧

namespace kiriyama::web {

std::vector<InstanceInfo> instances() {
    std::vector<InstanceInfo> out;
    out.reserve(gSlots.size());
    for (const auto& [handle, slot] : gSlots) {
        if (slot != nullptr) {
            out.push_back(InstanceInfo{slot->handle, slot->type, slot->visible, slot->frame});
        }
    }
    return out;
}

const core::ChartTheme& currentTheme() {
    return gTheme;
}

void pumpPie(int handle, PieChart& chart) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    PieState& pie = slot->pie;
    if (pie.styleDirty) {
        pie.styleDirty = false;
        core::PieStyle nextStyle = pie.style;
        nextStyle.textMeasure = chart.style().textMeasure;
        chart.setStyle(nextStyle);
    }
    if (pie.dataDirty) {
        pie.dataDirty = false;
        chart.setData(pie.data);  // 组件持有 model（拷贝），槽里这份只是 pending 缓冲
        pie.data.slices.clear();
    }
    pie.sliceCount = static_cast<int>(chart.sliceCount());
}

void pumpSparkBar(int handle, scene::SparkBar& bar) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || !slot->spark.dirty) {
        return;
    }
    SparkState& spark = slot->spark;
    spark.dirty = false;
    bar.setOptions(spark.options);
    bar.setColors(spark.colors);
    bar.setData(std::move(spark.data));  // 组件自己持有 model（移动接管）
    spark.data = {};
}

void pumpTable(int handle, scene::TableWidget& table) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr || !slot->table.dirty) {
        return;
    }
    slot->table.dirty = false;
    table.setData(std::move(slot->table.data));  // 组件自己持有 model（移动接管）
    slot->table.data = {};
}

void pumpLine(int handle, scene::LineChart& chart) {
    Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    LineState& line = slot->line;
    if (line.configDirty) {
        line.configDirty = false;
        scene::LineChartConfig nextCfg = line.config;
        nextCfg.textMeasure = chart.config().textMeasure;
        nextCfg.layout.measureText = nextCfg.textMeasure;
        chart.setConfig(nextCfg);
        chart.setPalette(line.palette);
    }
    if (line.dataDirty) {
        line.dataDirty = false;
        chart.setData(std::move(line.data));
        line.data = {};
    }
}

void applyTooltip(int handle, TooltipHost& tooltip) {
    const Slot* slot = findSlot(handle);
    if (slot == nullptr) {
        return;
    }
    const TooltipState& tip = slot->tip;

    tooltip.setFollowPointer(tip.followPointer);

    core::TooltipOptions options = tooltip.renderer().options();
    options.arrowWidth = tip.arrowWidth;
    if (tip.side >= 0) {
        options.side = static_cast<core::TooltipSide>(tip.side);
    } else if (tip.followPointer) {
        // 跟随鼠标时贴在光标右侧：不挡指针，也不会因为靠近画布顶部而翻面。
        options.side = core::TooltipSide::Right;
    }
    tooltip.setOptions(options);

    TooltipStyle style = tooltip.renderer().style();
    if (tip.fontSize > 0.0) {
        style.titleSize = tip.fontSize;
        style.bodySize = tip.fontSize > 10.0 ? tip.fontSize - 1.0 : tip.fontSize;
    }
    tooltip.setStyle(style);
}

void reportHoveredSlice(int handle, int index) {
    if (Slot* slot = findSlot(handle)) {
        slot->pie.hovered = index;
    }
}

void reportHoveredLinePoint(int handle, int seriesIndex, int pointIndex) {
    if (Slot* slot = findSlot(handle)) {
        slot->line.hoveredSeries = seriesIndex;
        slot->line.hoveredPoint = pointIndex;
    }
}

}  // namespace kiriyama::web
