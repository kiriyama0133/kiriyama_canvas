#pragma once

// 图表数据模型：折线 / 柱状共用 Dataset，饼图用 PieChartData。
//
// 空值处理约定：
//   - DataPoint::hasValue == false 表示缺失（JSON 的 null / undefined）。
//   - NaN、inf 不参与任何边界计算，sanitize() 会把它们转成缺失并记录到 DataIssues。
//   - 缺失点不会让整条系列失效：折线在该处断开，柱状跳过该柱，饼图跳过该片。

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "kiriyama/math/Range.h"

namespace kiriyama::core {

using math::RangeD;

// ---------------------------------------------------------------- 数据点

struct DataPoint {
    double x{0.0};
    double y{0.0};
    bool hasValue{false};

    [[nodiscard]] static constexpr DataPoint Value(double xValue, double yValue) noexcept {
        return {xValue, yValue, true};
    }

    /// 缺失值仍然保留 x，折线靠它定位断点。
    [[nodiscard]] static constexpr DataPoint Null(double xValue) noexcept {
        return {xValue, 0.0, false};
    }

    /// 有值且有限才算可用。
    [[nodiscard]] bool valid() const noexcept {
        return hasValue && std::isfinite(x) && std::isfinite(y);
    }
};

/// 数据清洗统计。调用方可以据此提示"3 个点被跳过"。
struct DataIssues {
    std::size_t nullValues{0};       ///< 显式空值
    std::size_t nonFiniteValues{0};  ///< NaN / inf，已被转成空值
    std::size_t negativeValues{0};   ///< 饼图 / 对数轴中不可用的负值
    std::size_t emptySeries{0};      ///< 没有任何点的系列

    [[nodiscard]] constexpr std::size_t total() const noexcept {
        return nullValues + nonFiniteValues + negativeValues + emptySeries;
    }

    [[nodiscard]] constexpr bool hasIssues() const noexcept { return total() > 0; }

    constexpr void merge(const DataIssues& other) noexcept {
        nullValues += other.nullValues;
        nonFiniteValues += other.nonFiniteValues;
        negativeValues += other.negativeValues;
        emptySeries += other.emptySeries;
    }
};

// ---------------------------------------------------------------- 系列 / 数据集

struct Series {
    std::string name;
    std::vector<DataPoint> points;

    void addPoint(double x, double y) { points.push_back(DataPoint::Value(x, y)); }
    void addNull(double x) { points.push_back(DataPoint::Null(x)); }

    [[nodiscard]] std::size_t validCount() const noexcept {
        std::size_t count = 0;
        for (const DataPoint& point : points) {
            if (point.valid()) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool empty() const noexcept { return points.empty(); }
};

/// 折线图与柱状图共用的数据集。
/// 折线：点的 x 是数据值（时间 / 数值轴）。
/// 柱状：x 是类别索引，标签放在 categories 里。
struct Dataset {
    std::vector<std::string> categories;
    std::vector<Series> series;

    [[nodiscard]] bool empty() const noexcept { return series.empty(); }

    /// 类别数：优先用 categories，其次取最长系列的点数。
    [[nodiscard]] std::size_t categoryCount() const noexcept;

    [[nodiscard]] std::size_t maxPointCount() const noexcept;

    [[nodiscard]] std::size_t validCount() const noexcept;

    /// 把 NaN / inf 转成缺失值，并统计空值。可重复调用。
    void sanitize(DataIssues* issues = nullptr);
};

enum class BarMode {
    None,     ///< 不是柱状图：x 按数据的数值轴处理（折线 / 散点）
    Grouped,  ///< 同类别多系列并排
    Stacked,  ///< 同类别多系列堆叠，正负分开累计
};

// ---------------------------------------------------------------- 边界

struct DataBounds {
    RangeD x{0.0, 0.0};
    RangeD y{0.0, 0.0};
    bool hasData{false};
};

/// 数据边界。
/// 折线：所有有效点的 x / y 外接范围。
/// 柱状：x 固定为类别带 [-0.5, n-0.5]，y 强制包含 0 基线；堆叠时按类别累计正负值。
[[nodiscard]] DataBounds computeBounds(const Dataset& dataset,
                                       BarMode mode = BarMode::None);

// ---------------------------------------------------------------- 原始数据接入

/// 表格型原始输入：一列类目 + 若干列数值，nullopt 表示缺失。
struct RawSeries {
    std::string name;
    std::vector<std::optional<double>> values;
};

/// 把"类目 × 数值列"的原始数据转成 Dataset（柱状图常用）。
/// - 类目为空时按最长列生成 "1"、"2"…
/// - 数值列比类目长时自动补空类目，不丢数据
/// - nullopt / NaN / inf 统一转成缺失点，并累计到 issues
[[nodiscard]] Dataset makeCategoryDataset(std::vector<std::string> categories,
                                          std::vector<RawSeries> series,
                                          DataIssues* issues = nullptr);

// ---------------------------------------------------------------- 饼图

struct PieSlice {
    std::string label;
    double value{0.0};
};

struct PieChartData {
    std::vector<PieSlice> slices;

    /// 非有限值、负值、零值都会被排除在统计之外。
    void sanitize(DataIssues* issues = nullptr);

    /// 只累加正的有限值。
    [[nodiscard]] double total() const noexcept;

    /// 各片占比，与 slices 等长；不可用的片为 0。总和为 1（total > 0 时）。
    [[nodiscard]] std::vector<double> fractions() const;

    [[nodiscard]] std::size_t usableCount() const noexcept;

    [[nodiscard]] bool empty() const noexcept { return slices.empty(); }
};

}  // namespace kiriyama::core
