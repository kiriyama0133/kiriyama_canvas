#pragma once

// 表格数据模型 + 排序状态机。
//
// 分工（和 VirtualScroll / InteractionState 同构）：
//   本模块  持有"视图状态"（当前排序、选中行），并维护行置换 order[]
//   调用方  持有"数据"（TableData），通过 attach() 交给本模块只读引用
//   渲染层  读 order() 决定画哪一行的第几条数据，自己不做排序
//
// 三条关键约定：
//   1. 表格**不持有数据**。TableData 永远归调用方，这里只留一个 const 指针 +
//      调用方显式给的 dataVersion。拷贝一份就等于有了第二个真相源。
//   2. 排序**不重排数据**，只重排 order[]（行下标置换）。这样行身份稳定，
//      选中 / 动画 / SceneDiff 的 key 全部不受影响；100 万行移动的是 4 字节而不是整行。
//   3. 排序值是**类型化**的，不是渲染用的字符串。数字列按 double / int64 比较，
//      否则 "10" < "9"。显示串由列的 format 在渲染时生成。
//
// 空值位置固定：默认永远在末尾，不随升 / 降序翻转。否则点一次降序，
// 整块空行就从表底跳到表头，观感上像表格坏了。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kiriyama::core {

/// 没有选中行 / 位置非法。
inline constexpr int kNoRow = -1;
/// 列下标非法。
inline constexpr std::size_t kColumnNotFound = static_cast<std::size_t>(-1);

// ---------------------------------------------------------------- 单元格

/// 单元格的值。monostate = 缺失（空单元格）。
/// Date 用 double 存 epoch 秒；Integer 单独一条分支，避免大整数被 double 吃掉精度。
using CellValue = std::variant<std::monostate, double, std::int64_t, std::string, bool>;

struct TableCell {
    CellValue value{};

    TableCell() = default;
    TableCell(double v) : value(v) {}
    TableCell(std::int64_t v) : value(v) {}
    TableCell(int v) : value(static_cast<std::int64_t>(v)) {}
    TableCell(std::string v) : value(std::move(v)) {}
    TableCell(const char* v) : value(std::string(v)) {}
    TableCell(bool v) : value(v) {}

    [[nodiscard]] static TableCell Missing() noexcept { return TableCell{}; }
    [[nodiscard]] bool missing() const noexcept {
        return std::holds_alternative<std::monostate>(value);
    }
};

/// 列的类型决定比较器。**必须与塞进去的 variant 分支一致**——
/// 类型不匹配按缺失处理，而不是悄悄转成字符串（那样排序会静默变味）。
enum class CellType : std::uint8_t {
    Text,     ///< 字符串，按 textCompare 比较（默认 UTF-8 字节序 = 码点序）
    Number,   ///< 浮点
    Integer,  ///< 整数，精确比较
    Bool,     ///< false < true
    Date,     ///< epoch 秒，按浮点比较
};

/// 列定义。id 是稳定标识，不要用下标——列可以重排、可以隐藏，
/// 下标一变排序就丢了。下标只在这里的 TableData::columnIndex() 里临时算。
struct TableColumn {
    std::string id;
    std::string title;
    /// 期望宽度。自动列宽由 src 层量出字体后覆盖（core 不碰字体度量）。
    double width{120.0};
    double minWidth{48.0};
    CellType type{CellType::Text};
    bool sortable{true};
    bool resizable{true};
    /// 数字列的显示格式，如 "%.2f"；空表示由渲染层决定。
    std::string format;

    [[nodiscard]] bool valid() const noexcept { return !id.empty(); }
};

/// 一行。cells 允许比 columns 短，缺的部分按空单元格渲染。
struct TableRow {
    std::string key;  ///< 行身份。排序后靠它找回选中，也是 SceneDiff 的节点 key。
    std::vector<TableCell> cells;

    [[nodiscard]] const TableCell* cell(std::size_t index) const noexcept {
        return index < cells.size() ? &cells[index] : nullptr;
    }
};

struct TableData {
    std::vector<TableColumn> columns;
    std::vector<TableRow> rows;

    [[nodiscard]] bool empty() const noexcept { return rows.empty(); }
    [[nodiscard]] std::size_t rowCount() const noexcept { return rows.size(); }
    [[nodiscard]] std::size_t columnCount() const noexcept { return columns.size(); }

    /// 找不到返回 kColumnNotFound。
    [[nodiscard]] std::size_t columnIndex(std::string_view id) const noexcept;
    [[nodiscard]] const TableColumn* findColumn(std::string_view id) const noexcept;

    /// 行 key -> 行下标。找不到返回 kNoRow。
    [[nodiscard]] int rowIndex(std::string_view key) const noexcept;
};

// ---------------------------------------------------------------- 排序

enum class SortDir : std::uint8_t {
    None,  ///< 未排序：order 是恒等置换，恢复数据原始顺序
    Asc,
    Desc,
};

struct TableSort {
    std::string columnId;  ///< 空 = 未排序
    SortDir dir{SortDir::None};

    [[nodiscard]] bool active() const noexcept {
        return dir != SortDir::None && !columnId.empty();
    }
    [[nodiscard]] bool operator==(const TableSort&) const noexcept = default;
};

/// Local：本模块自己排 order[]。
/// Remote：不排序，order 保持恒等——排序由调用方拿到 sortChanged 后重新取数，
///         本模块只负责记住"用户想要按哪列、什么方向排"。
enum class SortMode : std::uint8_t { Local, Remote };

struct TableOptions {
    SortMode mode{SortMode::Local};
    /// true 时空值排在最前，false（默认）排最后。不随升 / 降序翻转。
    bool nullsFirst{false};

    /// 文本比较器。默认 std::string_view::compare（UTF-8 字节序）。
    /// 中文按拼音排序、忽略大小写、忽略重音都在这里注入——core 不引入 ICU。
    using TextCompare = int (*)(std::string_view, std::string_view, void* context);
    TextCompare textCompare{nullptr};
    void* textCompareContext{nullptr};
};

/// 一次操作造成的变化。调用方据此决定重绘 / 重新取数 / 移动选中高亮。
struct TableChange {
    /// 事件是否被接受（点不可排序的列、未知列 id 都是 false）。
    bool accepted{false};
    bool sortChanged{false};
    /// order 需要重新读取。注意"内容可能和原来一样"，实际重绘交给 diff 判断。
    bool orderChanged{false};
    bool selectionChanged{false};

    TableSort sort{};
    int selectedPosition{kNoRow};
    std::string selectedRowKey;
};

// ---------------------------------------------------------------- 比较

/// 升 / 降序共用的比较器：返回 <0 / 0 / >0。
///
/// dir 只作用于**非空值**；空值的固定位置（nullsFirst）不随方向翻转。
/// 这一点是刻意收进来的——如果调用方自己写 `dir == Desc ? -cmp : cmp`，
/// 空行就会在升 / 降序之间跳边，是表格最常见的观感 bug。
[[nodiscard]] int compareCells(const TableCell& a, const TableCell& b, CellType type,
                               SortDir dir = SortDir::Asc,
                               const TableOptions& options = {});

/// 列是否可参与排序（列存在且 sortable）。
[[nodiscard]] bool isSortableColumn(const TableData& data, std::string_view columnId) noexcept;

// ---------------------------------------------------------------- 状态

class TableState {
public:
    TableState() = default;
    explicit TableState(TableOptions options);

    [[nodiscard]] const TableOptions& options() const noexcept { return options_; }
    void setOptions(TableOptions options);

    // ---- 数据绑定

    /// 绑定数据。同一个对象且 version 未变 → 直接返回，不重排。
    /// 改了行内容但不换对象时必须自己把 version +1（和 VirtualScroll::setItemCount 同思路：
    /// 让调用方声明"变了"，不靠每帧深比较）。
    void attach(const TableData& data, std::uint64_t dataVersion = 0);

    /// 解绑（树被销毁时调）。
    void detach() noexcept;

    [[nodiscard]] const TableData* data() const noexcept { return data_; }
    [[nodiscard]] std::uint64_t dataVersion() const noexcept { return version_; }

    /// 数据没换版本但内容变了时，强制重排。
    void invalidate();

    // ---- 排序

    /// 点表头：asc → desc → 未排序。不可排序的列返回 accepted == false。
    TableChange cycleSort(std::string_view columnId);

    /// 直接设置。列 id 未知或该列不可排序时 accepted == false，状态不变。
    /// dir == None 或列 id 为空都归一成"未排序"。
    TableChange setSort(TableSort sort);

    TableChange clearSort();

    [[nodiscard]] TableSort sort() const noexcept { return sort_; }
    [[nodiscard]] bool sorted() const noexcept { return sort_.active(); }
    [[nodiscard]] bool isSortedBy(std::string_view columnId) const noexcept {
        return sort_.active() && sort_.columnId == columnId;
    }
    [[nodiscard]] bool canSort(std::string_view columnId) const noexcept;
    /// 某列当前的排序方向；不是当前排序列时返回 None。
    [[nodiscard]] SortDir directionOf(std::string_view columnId) const noexcept;

    // ---- 选中

    /// 按行 key 选中。不存在也接受，位置为 kNoRow（远程分页回来还能接上）。
    TableChange setSelection(std::string_view rowKey);
    /// 按显示位置选中（点击行时用，内部转成 key）。
    TableChange selectPosition(int position);
    TableChange clearSelection();

    [[nodiscard]] bool hasSelection() const noexcept { return !selectedKey_.empty(); }
    [[nodiscard]] std::string_view selectedRowKey() const noexcept { return selectedKey_; }
    /// 选中行在 order 里的位置，不在当前数据里时为 kNoRow。
    [[nodiscard]] int selectedPosition() const noexcept { return selectedPos_; }

    // ---- 行置换

    /// order()[i] = 第 i 个可见行在 data().rows 里的下标。恒等置换表示未排序。
    [[nodiscard]] std::span<const int> order() const noexcept { return order_; }
    [[nodiscard]] std::size_t rowCount() const noexcept { return order_.size(); }
    [[nodiscard]] bool identityOrder() const noexcept;

    /// 按显示顺序取行；越界返回 nullptr。
    [[nodiscard]] const TableRow* rowAt(int position) const noexcept;
    /// 行 key 在 order 里的位置；找不到 kNoRow。
    [[nodiscard]] int positionOf(std::string_view rowKey) const noexcept;

    /// order 的代数，每次重排 +1。渲染层拿它判缓存失效。
    [[nodiscard]] std::uint64_t orderVersion() const noexcept { return orderVersion_; }

private:
    void rebuildOrder();
    [[nodiscard]] bool remote() const noexcept { return options_.mode == SortMode::Remote; }

    TableOptions options_{};
    const TableData* data_{nullptr};
    std::uint64_t version_{0};
    std::uint64_t orderVersion_{0};

    TableSort sort_{};
    std::vector<int> order_;

    std::string selectedKey_;
    int selectedPos_{kNoRow};
};

}  // namespace kiriyama::core
