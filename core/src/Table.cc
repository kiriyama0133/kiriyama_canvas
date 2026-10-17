#include "kiriyama/core/Table.h"

namespace kiriyama::core {
namespace {

/// 各类型的取值器。返回 nullopt = "按缺失处理"：
/// monostate、非有限浮点、以及**类型不匹配**（字符串列里塞了 double 之类）。
/// 类型不匹配按缺失而不是按字符串转换，是为了让错误尽早显形，而不是静默排出一个怪序。

std::optional<double> asNumber(const TableCell& cell) {
    if (const auto* value = std::get_if<double>(&cell.value)) {
        return std::isfinite(*value) ? std::optional<double>(*value) : std::nullopt;
    }
    if (const auto* value = std::get_if<std::int64_t>(&cell.value)) {
        return static_cast<double>(*value);
    }
    return std::nullopt;
}

std::optional<std::int64_t> asInteger(const TableCell& cell) {
    if (const auto* value = std::get_if<std::int64_t>(&cell.value)) {
        return *value;
    }
    if (const auto* value = std::get_if<double>(&cell.value)) {
        if (!std::isfinite(*value)) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(*value);
    }
    return std::nullopt;
}

std::optional<bool> asBool(const TableCell& cell) {
    if (const auto* value = std::get_if<bool>(&cell.value)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<std::string_view> asText(const TableCell& cell) {
    if (const auto* value = std::get_if<std::string>(&cell.value)) {
        return std::string_view(*value);
    }
    return std::nullopt;
}

[[nodiscard]] int sign(int value) noexcept { return (value > 0) - (value < 0); }

[[nodiscard]] bool missingFor(const TableCell& cell, CellType type) noexcept {
    switch (type) {
        case CellType::Text:
            return !asText(cell).has_value();
        case CellType::Integer:
            return !asInteger(cell).has_value();
        case CellType::Bool:
            return !asBool(cell).has_value();
        case CellType::Number:
        case CellType::Date:
            return !asNumber(cell).has_value();
    }
    return true;
}

/// 两边都已确认非缺失时调用。
[[nodiscard]] int compareValues(const TableCell& a, const TableCell& b, CellType type,
                                const TableOptions& options) {
    switch (type) {
        case CellType::Text: {
            const std::optional<std::string_view> left = asText(a);
            const std::optional<std::string_view> right = asText(b);
            if (!left || !right) {
                return 0;
            }
            if (options.textCompare != nullptr) {
                return sign(options.textCompare(*left, *right, options.textCompareContext));
            }
            return sign(left->compare(*right));
        }
        case CellType::Integer: {
            const std::optional<std::int64_t> left = asInteger(a);
            const std::optional<std::int64_t> right = asInteger(b);
            if (!left || !right) {
                return 0;
            }
            return *left < *right ? -1 : (*left > *right ? 1 : 0);
        }
        case CellType::Bool: {
            const std::optional<bool> left = asBool(a);
            const std::optional<bool> right = asBool(b);
            if (!left || !right) {
                return 0;
            }
            return *left == *right ? 0 : (*left ? 1 : -1);
        }
        case CellType::Number:
        case CellType::Date:
        default: {
            const std::optional<double> left = asNumber(a);
            const std::optional<double> right = asNumber(b);
            if (!left || !right) {
                return 0;
            }
            return *left < *right ? -1 : (*left > *right ? 1 : 0);
        }
    }
}

/// 稳定排序用的判定（严格弱序）。返回是否 a 应排在 b 之前。
[[nodiscard]] bool lessForSort(const TableCell& a, const TableCell& b, CellType type,
                               SortDir dir, const TableOptions& options) {
    const bool aMissing = missingFor(a, type);
    const bool bMissing = missingFor(b, type);
    if (aMissing || bMissing) {
        if (aMissing && bMissing) {
            return false;
        }
        // 空值位置固定：nullsFirst 时排最前，否则排最后。**不随 dir 翻转**。
        const bool aFirst = aMissing == options.nullsFirst;
        return aFirst;
    }
    int result = compareValues(a, b, type, options);
    if (dir == SortDir::Desc) {
        result = -result;
    }
    return result < 0;
}

}  // namespace

// ---------------------------------------------------------------- TableData

std::size_t TableData::columnIndex(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].id == id) {
            return i;
        }
    }
    return kColumnNotFound;
}

const TableColumn* TableData::findColumn(std::string_view id) const noexcept {
    const std::size_t index = columnIndex(id);
    return index == kColumnNotFound ? nullptr : &columns[index];
}

int TableData::rowIndex(std::string_view key) const noexcept {
    if (key.empty()) {
        return kNoRow;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return kNoRow;
}

// ---------------------------------------------------------------- 比较

int compareCells(const TableCell& a, const TableCell& b, CellType type, SortDir dir,
                 const TableOptions& options) {
    const bool aMissing = missingFor(a, type);
    const bool bMissing = missingFor(b, type);
    if (aMissing || bMissing) {
        if (aMissing && bMissing) {
            return 0;
        }
        const int missingRank = options.nullsFirst ? -1 : 1;
        return aMissing ? missingRank : -missingRank;
    }
    const int result = compareValues(a, b, type, options);
    return dir == SortDir::Desc ? -result : result;
}

bool isSortableColumn(const TableData& data, std::string_view columnId) noexcept {
    if (columnId.empty()) {
        return false;
    }
    const TableColumn* column = data.findColumn(columnId);
    return column != nullptr && column->sortable;
}

// ---------------------------------------------------------------- TableState

TableState::TableState(TableOptions options) : options_(options) {}

void TableState::setOptions(TableOptions options) {
    options_ = options;
    // nullsFirst / mode 都会影响次序，统一重排一次，不做差异判断。
    if (data_ != nullptr) {
        rebuildOrder();
    }
}

void TableState::attach(const TableData& data, std::uint64_t dataVersion) {
    if (data_ == &data && version_ == dataVersion) {
        return;
    }
    data_ = &data;
    version_ = dataVersion;
    rebuildOrder();
}

void TableState::detach() noexcept {
    data_ = nullptr;
    order_.clear();
    selectedPos_ = kNoRow;
    ++orderVersion_;
}

void TableState::invalidate() {
    if (data_ != nullptr) {
        rebuildOrder();
    }
}

TableChange TableState::cycleSort(std::string_view columnId) {
    TableChange change;
    change.sort = sort_;
    change.selectedPosition = selectedPos_;
    change.selectedRowKey = selectedKey_;

    if (!canSort(columnId)) {
        return change;  // accepted 保持 false
    }

    TableSort next{std::string(columnId), SortDir::Asc};
    if (sort_.columnId == columnId) {
        // 三态循环。Desc 之后回到 None，也就是恢复数据原始顺序。
        next.dir = sort_.dir == SortDir::Asc ? SortDir::Desc : SortDir::None;
    }
    return setSort(next);
}

TableChange TableState::setSort(TableSort sort) {
    if (sort.dir == SortDir::None) {
        sort.columnId.clear();
    }
    if (sort.columnId.empty()) {
        sort.dir = SortDir::None;
    }

    TableChange change;
    change.sort = sort_;
    change.selectedPosition = selectedPos_;
    change.selectedRowKey = selectedKey_;

    // 非空列名必须存在且可排序，否则拒绝，不静默清空排序。
    if (sort.active() && !canSort(sort.columnId)) {
        return change;
    }

    if (sort == sort_) {
        change.accepted = true;
        return change;
    }

    sort_ = sort;
    rebuildOrder();

    change.accepted = true;
    change.sortChanged = true;
    change.orderChanged = true;
    change.sort = sort_;
    change.selectedPosition = selectedPos_;
    return change;
}

TableChange TableState::clearSort() { return setSort(TableSort{}); }

bool TableState::canSort(std::string_view columnId) const noexcept {
    if (data_ == nullptr) {
        return false;
    }
    return isSortableColumn(*data_, columnId);
}

SortDir TableState::directionOf(std::string_view columnId) const noexcept {
    return isSortedBy(columnId) ? sort_.dir : SortDir::None;
}

TableChange TableState::setSelection(std::string_view rowKey) {
    TableChange change;
    change.sort = sort_;
    change.selectedPosition = selectedPos_;
    change.selectedRowKey = selectedKey_;

    if (selectedKey_ == rowKey) {
        return change;
    }

    selectedKey_.assign(rowKey);
    selectedPos_ = positionOf(selectedKey_);

    change.accepted = true;
    change.selectionChanged = true;
    change.selectedPosition = selectedPos_;
    change.selectedRowKey = selectedKey_;
    return change;
}

TableChange TableState::selectPosition(int position) {
    if (position < 0 || static_cast<std::size_t>(position) >= order_.size()) {
        return clearSelection();
    }
    const TableRow* row = rowAt(position);
    return row == nullptr ? clearSelection() : setSelection(row->key);
}

TableChange TableState::clearSelection() {
    TableChange change;
    change.sort = sort_;
    change.selectedPosition = selectedPos_;
    change.selectedRowKey = selectedKey_;

    if (selectedKey_.empty()) {
        return change;
    }

    selectedKey_.clear();
    selectedPos_ = kNoRow;

    change.accepted = true;
    change.selectionChanged = true;
    change.selectedPosition = kNoRow;
    change.selectedRowKey.clear();
    return change;
}

bool TableState::identityOrder() const noexcept {
    for (std::size_t i = 0; i < order_.size(); ++i) {
        if (order_[i] != static_cast<int>(i)) {
            return false;
        }
    }
    return true;
}

const TableRow* TableState::rowAt(int position) const noexcept {
    if (data_ == nullptr || position < 0 ||
        static_cast<std::size_t>(position) >= order_.size()) {
        return nullptr;
    }
    const int rowIndexValue = order_[static_cast<std::size_t>(position)];
    if (rowIndexValue < 0 || static_cast<std::size_t>(rowIndexValue) >= data_->rows.size()) {
        return nullptr;
    }
    return &data_->rows[static_cast<std::size_t>(rowIndexValue)];
}

int TableState::positionOf(std::string_view rowKey) const noexcept {
    if (data_ == nullptr || rowKey.empty()) {
        return kNoRow;
    }
    for (std::size_t position = 0; position < order_.size(); ++position) {
        const int rowIndexValue = order_[position];
        if (rowIndexValue < 0 || static_cast<std::size_t>(rowIndexValue) >= data_->rows.size()) {
            continue;
        }
        if (data_->rows[static_cast<std::size_t>(rowIndexValue)].key == rowKey) {
            return static_cast<int>(position);
        }
    }
    return kNoRow;
}

void TableState::rebuildOrder() {
    order_.clear();
    ++orderVersion_;

    if (data_ == nullptr) {
        selectedPos_ = kNoRow;
        return;
    }

    const std::size_t count = data_->rows.size();
    order_.resize(count);
    std::iota(order_.begin(), order_.end(), 0);

    // Remote 模式不排序：调用方拿到 sortChanged 后自己换数据，order 保持恒等。
    const std::size_t columnIndexValue =
        sort_.active() ? data_->columnIndex(sort_.columnId) : kColumnNotFound;

    if (!remote() && columnIndexValue != kColumnNotFound &&
        data_->columns[columnIndexValue].sortable) {
        const CellType type = data_->columns[columnIndexValue].type;
        const SortDir dir = sort_.dir;
        const TableOptions options = options_;
        const TableRow* rows = data_->rows.data();
        const TableCell missingCell{};

        // key 相同的行永远保持数据原序：stable_sort + 只在 order[] 上重排。
        std::stable_sort(order_.begin(), order_.end(), [&](int lhs, int rhs) {
            const TableCell* left = rows[lhs].cell(columnIndexValue);
            const TableCell* right = rows[rhs].cell(columnIndexValue);
            return lessForSort(left != nullptr ? *left : missingCell,
                               right != nullptr ? *right : missingCell, type, dir, options);
        });
    }

    selectedPos_ = positionOf(selectedKey_);
}

}  // namespace kiriyama::core
