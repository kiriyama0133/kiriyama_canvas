#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "kiriyama/core/Chart.h"

namespace {

using kiriyama::core::CellType;
using kiriyama::core::kColumnNotFound;
using kiriyama::core::kNoRow;
using kiriyama::core::SortDir;
using kiriyama::core::SortMode;
using kiriyama::core::TableCell;
using kiriyama::core::TableColumn;
using kiriyama::core::TableData;
using kiriyama::core::TableOptions;
using kiriyama::core::TableRow;
using kiriyama::core::TableSort;
using kiriyama::core::TableState;
using kiriyama::core::compareCells;

[[nodiscard]] TableColumn column(std::string id, CellType type, bool sortable = true) {
    TableColumn result;
    result.id = std::move(id);
    result.title = result.id;
    result.type = type;
    result.sortable = sortable;
    return result;
}

[[nodiscard]] TableRow row(std::string key, std::vector<TableCell> cells) {
    TableRow result;
    result.key = std::move(key);
    result.cells = std::move(cells);
    return result;
}

/// name / price 两列。price 有重复值（b 和 d 都是 10），用来验证稳定排序。
TableData makeNumericTable() {
    TableData data;
    data.columns.push_back(column("name", CellType::Text));
    data.columns.push_back(column("price", CellType::Number));
    data.rows.push_back(row("a", {TableCell("a"), TableCell(30.0)}));
    data.rows.push_back(row("b", {TableCell("b"), TableCell(10.0)}));
    data.rows.push_back(row("c", {TableCell("c"), TableCell(20.0)}));
    data.rows.push_back(row("d", {TableCell("d"), TableCell(10.0)}));
    return data;
}

/// 含两个空值，用来验证空值位置。
TableData makeNullTable() {
    TableData data;
    data.columns.push_back(column("name", CellType::Text));
    data.columns.push_back(column("price", CellType::Number));
    data.rows.push_back(row("a", {TableCell("a"), TableCell(30.0)}));
    data.rows.push_back(row("b", {TableCell("b"), TableCell::Missing()}));
    data.rows.push_back(row("c", {TableCell("c"), TableCell(10.0)}));
    data.rows.push_back(row("d", {TableCell("d"), TableCell::Missing()}));
    return data;
}

/// 把 order 映射成行 key 序列，便于断言。
std::string orderKeys(const TableState& state) {
    std::string result;
    for (std::size_t i = 0; i < state.rowCount(); ++i) {
        const auto* current = state.rowAt(static_cast<int>(i));
        if (current == nullptr) {
            result += '?';
            continue;
        }
        result += current->key;
    }
    return result;
}

// ---------------------------------------------------------------- TableData

TEST(TableData, FindsColumnsByIdNotIndex) {
    TableData data = makeNumericTable();
    EXPECT_EQ(data.columnIndex("name"), 0u);
    EXPECT_EQ(data.columnIndex("price"), 1u);
    EXPECT_EQ(data.columnIndex("missing"), kColumnNotFound);
    EXPECT_NE(data.findColumn("price"), nullptr);
    EXPECT_EQ(data.findColumn("missing"), nullptr);
    EXPECT_EQ(data.columnCount(), 2u);
    EXPECT_EQ(data.rowCount(), 4u);
}

TEST(TableData, FindsRowsByKey) {
    TableData data = makeNumericTable();
    EXPECT_EQ(data.rowIndex("a"), 0);
    EXPECT_EQ(data.rowIndex("d"), 3);
    EXPECT_EQ(data.rowIndex("zzz"), kNoRow);
    EXPECT_EQ(data.rowIndex(""), kNoRow);
}

TEST(TableData, ShortRowYieldsNullCell) {
    TableData data;
    data.columns.push_back(column("name", CellType::Text));
    data.columns.push_back(column("price", CellType::Number));
    data.rows.push_back(row("only-name", {TableCell("x")}));
    EXPECT_NE(data.rows[0].cell(0), nullptr);
    EXPECT_EQ(data.rows[0].cell(1), nullptr);
}

// ---------------------------------------------------------------- 比较器

TEST(CompareCells, TextAscendingAndDescending) {
    const TableCell apple{"apple"};
    const TableCell banana{"banana"};
    EXPECT_LT(compareCells(apple, banana, CellType::Text, SortDir::Asc), 0);
    EXPECT_GT(compareCells(apple, banana, CellType::Text, SortDir::Desc), 0);
    EXPECT_EQ(compareCells(apple, apple, CellType::Text), 0);
}

TEST(CompareCells, NumbersCompareNumericallyNotLexically) {
    // 字符串比较下 "9" > "10"，数字比较必须反过来。这是类型化单元格的全部意义。
    EXPECT_GT(compareCells(TableCell("9"), TableCell("10"), CellType::Text), 0);
    const TableCell nine{9.0};
    const TableCell ten{10.0};
    EXPECT_LT(compareCells(nine, ten, CellType::Number, SortDir::Asc), 0);
    EXPECT_GT(compareCells(nine, ten, CellType::Number, SortDir::Desc), 0);
}

TEST(CompareCells, IntegerKeepsPrecisionBeyondDouble) {
    // 2^53 之后 double 无法区分，Integer 分支必须用 int64 精确比较。
    constexpr std::int64_t base = 9007199254740992;  // 2^53
    const TableCell lower{base};
    const TableCell higher{base + 1};
    EXPECT_LT(compareCells(lower, higher, CellType::Integer), 0);
    EXPECT_EQ(compareCells(lower, higher, CellType::Number), 0);  // double 分不出来
}

TEST(CompareCells, BoolsOrderFalseBeforeTrue) {
    EXPECT_LT(compareCells(TableCell(false), TableCell(true), CellType::Bool), 0);
    EXPECT_GT(compareCells(TableCell(false), TableCell(true), CellType::Bool, SortDir::Desc), 0);
}

TEST(CompareCells, MissingValuesSitLastByDefault) {
    const TableCell missing = TableCell::Missing();
    const TableCell value{5.0};
    EXPECT_GT(compareCells(missing, value, CellType::Number), 0);
    EXPECT_LT(compareCells(value, missing, CellType::Number), 0);
}

TEST(CompareCells, MissingPositionDoesNotFlipWithDirection) {
    // 空值固定在末尾，不随方向翻到前面——这是最常见的表格观感 bug。
    const TableCell missing = TableCell::Missing();
    const TableCell value{5.0};
    EXPECT_GT(compareCells(missing, value, CellType::Number, SortDir::Desc), 0);
}

TEST(CompareCells, NullsFirstOptionPutsMissingOnTop) {
    TableOptions options;
    options.nullsFirst = true;
    const TableCell missing = TableCell::Missing();
    const TableCell value{5.0};
    EXPECT_LT(compareCells(missing, value, CellType::Number, SortDir::Asc, options), 0);
    EXPECT_LT(compareCells(missing, value, CellType::Number, SortDir::Desc, options), 0);
}

TEST(CompareCells, BothMissingIsEqual) {
    const TableCell missing = TableCell::Missing();
    EXPECT_EQ(compareCells(missing, missing, CellType::Number), 0);
    EXPECT_EQ(compareCells(missing, missing, CellType::Text), 0);
}

TEST(CompareCells, TypeMismatchCountsAsMissing) {
    // 字符串列里塞 double：不猜、不转，直接当缺失。
    const TableCell number{42.0};
    const TableCell text{"z"};
    EXPECT_GT(compareCells(number, text, CellType::Text), 0);  // number 视为空 → 排后
}

TEST(CompareCells, EmptyStringIsAValueNotMissing) {
    const TableCell empty{""};
    const TableCell hello{"hello"};
    EXPECT_LT(compareCells(empty, hello, CellType::Text), 0);
}

TEST(CompareCells, NonFiniteNumberCountsAsMissing) {
    const TableCell nan{std::numeric_limits<double>::quiet_NaN()};
    const TableCell value{1.0};
    EXPECT_GT(compareCells(nan, value, CellType::Number), 0);
}

TEST(CompareCells, InjectedTextCompareWins) {
    TableOptions options;
    options.textCompare = [](std::string_view a, std::string_view b, void*) {
        std::string left(a);
        std::string right(b);
        std::transform(left.begin(), left.end(), left.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(right.begin(), right.end(), right.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return left.compare(right);
    };
    // "apple" vs "Banana"：字节序 'a'(97) > 'B'(66)，忽略大小写则 a < b。
    const TableCell apple{"apple"};
    const TableCell banana{"Banana"};
    EXPECT_GT(compareCells(apple, banana, CellType::Text, SortDir::Asc), 0);
    EXPECT_LT(compareCells(apple, banana, CellType::Text, SortDir::Asc, options), 0);
}

// ---------------------------------------------------------------- TableState

TEST(TableState, AttachProducesIdentityOrder) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    ASSERT_EQ(state.rowCount(), 4u);
    EXPECT_TRUE(state.identityOrder());
    EXPECT_EQ(orderKeys(state), "abcd");
    EXPECT_FALSE(state.sorted());
}

TEST(TableState, AttachSameVersionSkipsRebuild) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data, 7);
    const std::uint64_t before = state.orderVersion();
    state.attach(data, 7);
    EXPECT_EQ(state.orderVersion(), before);
    state.attach(data, 8);
    EXPECT_NE(state.orderVersion(), before);
}

TEST(TableState, InvalidateForcesRebuild) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data, 1);
    const std::uint64_t before = state.orderVersion();
    state.invalidate();
    EXPECT_NE(state.orderVersion(), before);
}

TEST(TableState, SortAscendingByNumber) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    const auto change = state.setSort({"price", SortDir::Asc});
    EXPECT_TRUE(change.accepted);
    EXPECT_TRUE(change.sortChanged);
    EXPECT_EQ(orderKeys(state), "bdca");
    EXPECT_TRUE(state.isSortedBy("price"));
    EXPECT_EQ(state.directionOf("price"), SortDir::Asc);
    EXPECT_EQ(state.directionOf("name"), SortDir::None);
}

TEST(TableState, SortDescendingByNumber) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    state.setSort({"price", SortDir::Desc});
    EXPECT_EQ(orderKeys(state), "acbd");
}

TEST(TableState, SortIsStableForEqualValues) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    state.setSort({"price", SortDir::Asc});
    // b 和 d 都是 10，必须保持数据原序（b 在前）。
    EXPECT_EQ(orderKeys(state), "bdca");
    state.setSort({"price", SortDir::Desc});
    EXPECT_EQ(orderKeys(state), "acbd");
}

TEST(TableState, CycleSortIsThreeState) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);

    auto change = state.cycleSort("price");
    EXPECT_TRUE(change.accepted);
    EXPECT_EQ(state.sort().dir, SortDir::Asc);
    EXPECT_EQ(orderKeys(state), "bdca");

    change = state.cycleSort("price");
    EXPECT_TRUE(change.sortChanged);
    EXPECT_EQ(state.sort().dir, SortDir::Desc);
    EXPECT_EQ(orderKeys(state), "acbd");

    change = state.cycleSort("price");
    EXPECT_TRUE(change.sortChanged);
    EXPECT_EQ(state.sort().dir, SortDir::None);
    EXPECT_TRUE(state.sort().columnId.empty());
    // 第三次必须回到数据原始顺序，不能"把降序再排一次"。
    EXPECT_EQ(orderKeys(state), "abcd");
    EXPECT_TRUE(state.identityOrder());
}

TEST(TableState, CycleSortSwitchesColumn) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    state.cycleSort("price");   // price asc
    state.cycleSort("name");    // 换列：从 asc 重新开始
    EXPECT_EQ(state.sort().columnId, "name");
    EXPECT_EQ(state.sort().dir, SortDir::Asc);
    EXPECT_EQ(orderKeys(state), "abcd");
}

TEST(TableState, CycleSortRejectsUnsortableColumn) {
    TableData data = makeNumericTable();
    data.columns[1].sortable = false;
    TableState state;
    state.attach(data);

    const auto change = state.cycleSort("price");
    EXPECT_FALSE(change.accepted);
    EXPECT_FALSE(change.sortChanged);
    EXPECT_TRUE(state.identityOrder());
    EXPECT_EQ(orderKeys(state), "abcd");
}

TEST(TableState, SetSortRejectsUnknownColumn) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    state.setSort({"price", SortDir::Asc});
    const std::string before = orderKeys(state);

    const auto change = state.setSort({"nope", SortDir::Desc});
    EXPECT_FALSE(change.accepted);
    EXPECT_EQ(orderKeys(state), before);
    EXPECT_EQ(state.sort().columnId, "price");  // 原排序不被清掉
}

TEST(TableState, SetSortIsIdempotent) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    state.setSort({"price", SortDir::Asc});
    const std::uint64_t before = state.orderVersion();
    const auto change = state.setSort({"price", SortDir::Asc});
    EXPECT_TRUE(change.accepted);
    EXPECT_FALSE(change.sortChanged);
    EXPECT_EQ(state.orderVersion(), before);
}

TEST(TableState, NullsStayLastInBothDirections) {
    TableData data = makeNullTable();
    TableState state;
    state.attach(data);

    state.setSort({"price", SortDir::Asc});
    EXPECT_EQ(orderKeys(state), "cabd");

    state.setSort({"price", SortDir::Desc});
    EXPECT_EQ(orderKeys(state), "acbd");
}

TEST(TableState, NullsFirstReordersBothDirections) {
    TableData data = makeNullTable();
    TableOptions options;
    options.nullsFirst = true;
    TableState state(options);
    state.attach(data);

    state.setSort({"price", SortDir::Asc});
    EXPECT_EQ(orderKeys(state), "bdca");
    state.setSort({"price", SortDir::Desc});
    EXPECT_EQ(orderKeys(state), "bdac");
}

TEST(TableState, ShortRowSortsAsMissing) {
    TableData data;
    data.columns.push_back(column("name", CellType::Text));
    data.columns.push_back(column("price", CellType::Number));
    data.rows.push_back(row("has", {TableCell("has"), TableCell(1.0)}));
    data.rows.push_back(row("short", {TableCell("short")}));  // 少一格

    TableState state;
    state.attach(data);
    state.setSort({"price", SortDir::Asc});
    EXPECT_EQ(orderKeys(state), "hasshort");  // 缺失排最后
}

TEST(TableState, SelectionFollowsRowAcrossSort) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);

    state.selectPosition(3);  // 第四行 = d
    EXPECT_EQ(state.selectedRowKey(), "d");
    EXPECT_EQ(state.selectedPosition(), 3);

    const auto change = state.setSort({"price", SortDir::Asc});  // b d c a
    EXPECT_EQ(change.selectedRowKey, "d");
    EXPECT_EQ(change.selectedPosition, 1);
    EXPECT_EQ(state.selectedPosition(), 1);
    EXPECT_EQ(state.positionOf("d"), 1);
}

TEST(TableState, SelectionSurvivesRebuildWhenKeyStillExists) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data, 1);
    state.setSelection("c");

    state.attach(data, 2);  // 版本变了但 c 还在
    EXPECT_TRUE(state.hasSelection());
    EXPECT_EQ(state.selectedRowKey(), "c");
    EXPECT_EQ(state.selectedPosition(), 2);
}

TEST(TableState, SelectionBecomesInvisibleWhenRowVanishes) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data, 1);
    state.setSelection("d");

    TableData smaller;
    smaller.columns = data.columns;
    smaller.rows.push_back(data.rows[0]);
    smaller.rows.push_back(data.rows[1]);
    state.attach(smaller, 2);

    // key 保留（远程分页回来还能接上），但位置失效。
    EXPECT_TRUE(state.hasSelection());
    EXPECT_EQ(state.selectedRowKey(), "d");
    EXPECT_EQ(state.selectedPosition(), kNoRow);
}

TEST(TableState, SelectPositionOutOfRangeClearsSelection) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    state.setSelection("a");
    state.selectPosition(99);
    EXPECT_FALSE(state.hasSelection());
    EXPECT_EQ(state.selectedPosition(), kNoRow);
}

TEST(TableState, RemoteModeKeepsIdentityOrder) {
    TableData data = makeNumericTable();
    TableOptions options;
    options.mode = SortMode::Remote;
    TableState state(options);
    state.attach(data);

    const auto change = state.cycleSort("price");
    EXPECT_TRUE(change.accepted);
    EXPECT_TRUE(change.sortChanged);
    EXPECT_EQ(change.sort.dir, SortDir::Asc);   // 意图被记住
    EXPECT_EQ(orderKeys(state), "abcd");        // 但顺序不动，由调用方换数据
    EXPECT_TRUE(state.identityOrder());
}

TEST(TableState, DetachClearsOrder) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    state.setSelection("a");
    state.detach();
    EXPECT_EQ(state.rowCount(), 0u);
    EXPECT_EQ(state.data(), nullptr);
    EXPECT_EQ(state.selectedPosition(), kNoRow);
    EXPECT_EQ(state.rowAt(0), nullptr);
}

TEST(TableState, DetachedStateCannotSort) {
    TableState state;
    const auto change = state.cycleSort("price");
    EXPECT_FALSE(change.accepted);
    EXPECT_FALSE(state.canSort("price"));
}

TEST(TableState, RowAtIsNullWhenOutOfRange) {
    TableData data = makeNumericTable();
    TableState state;
    state.attach(data);
    EXPECT_EQ(state.rowAt(-1), nullptr);
    EXPECT_EQ(state.rowAt(4), nullptr);
    ASSERT_NE(state.rowAt(0), nullptr);
    EXPECT_EQ(state.rowAt(0)->key, "a");
}

TEST(TableState, SortByTextColumn) {
    TableData data;
    data.columns.push_back(column("name", CellType::Text));
    data.rows.push_back(row("r1", {TableCell("Zebra")}));
    data.rows.push_back(row("r2", {TableCell("apple")}));
    data.rows.push_back(row("r3", {TableCell("Mango")}));

    TableState state;
    state.attach(data);
    state.setSort({"name", SortDir::Asc});
    // 默认字节序：大写字母全部排在小写前面。
    EXPECT_EQ(orderKeys(state), "r3r1r2");
}

TEST(TableState, SortByIntegerColumnIsExact) {
    TableData data;
    data.columns.push_back(column("id", CellType::Integer));
    data.rows.push_back(row("big", {TableCell(static_cast<std::int64_t>(9007199254740993LL))}));
    data.rows.push_back(row("small", {TableCell(static_cast<std::int64_t>(9007199254740992LL))}));

    TableState state;
    state.attach(data);
    state.setSort({"id", SortDir::Asc});
    EXPECT_EQ(orderKeys(state), "smallbig");
}

TEST(TableState, EmptyTableStaysEmpty) {
    TableData data;
    data.columns.push_back(column("name", CellType::Text));
    TableState state;
    state.attach(data);
    EXPECT_EQ(state.rowCount(), 0u);
    EXPECT_TRUE(state.identityOrder());
    state.cycleSort("name");
    EXPECT_EQ(state.rowCount(), 0u);
}

}  // namespace
