#include "kiriyama/core/Data.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kiriyama::core {

// ---------------------------------------------------------------- Dataset

std::size_t Dataset::maxPointCount() const noexcept {
    std::size_t count = 0;
    for (const Series& one : series) {
        count = std::max(count, one.points.size());
    }
    return count;
}

std::size_t Dataset::categoryCount() const noexcept {
    return categories.empty() ? maxPointCount() : categories.size();
}

std::size_t Dataset::validCount() const noexcept {
    std::size_t count = 0;
    for (const Series& one : series) {
        count += one.validCount();
    }
    return count;
}

void Dataset::sanitize(DataIssues* issues) {
    DataIssues local;

    for (Series& one : series) {
        if (one.points.empty()) {
            ++local.emptySeries;
        }
        for (DataPoint& point : one.points) {
            if (!point.hasValue) {
                ++local.nullValues;
                continue;
            }
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                // 非有限值不是数据，按缺失处理——继续参与计算会污染整条轴。
                point.hasValue = false;
                ++local.nonFiniteValues;
            }
        }
    }

    if (issues != nullptr) {
        issues->merge(local);
    }
}

// ---------------------------------------------------------------- 原始数据接入

Dataset makeCategoryDataset(std::vector<std::string> categories,
                            std::vector<RawSeries> series, DataIssues* issues) {
    std::size_t width = categories.size();
    for (const RawSeries& raw : series) {
        width = std::max(width, raw.values.size());
    }

    if (categories.size() < width) {
        categories.reserve(width);
        for (std::size_t i = categories.size(); i < width; ++i) {
            categories.push_back(std::to_string(i + 1));
        }
    }

    Dataset dataset;
    dataset.categories = std::move(categories);
    dataset.series.reserve(series.size());

    DataIssues local;
    for (RawSeries& raw : series) {
        Series one;
        one.name = std::move(raw.name);
        one.points.reserve(raw.values.size());

        for (std::size_t i = 0; i < raw.values.size(); ++i) {
            const double index = static_cast<double>(i);
            if (!raw.values[i].has_value()) {
                one.addNull(index);
                ++local.nullValues;
                continue;
            }

            const double value = *raw.values[i];
            if (!std::isfinite(value)) {
                one.addNull(index);
                ++local.nonFiniteValues;
                continue;
            }
            one.addPoint(index, value);
        }

        if (one.points.empty()) {
            ++local.emptySeries;
        }
        dataset.series.push_back(std::move(one));
    }

    if (issues != nullptr) {
        issues->merge(local);
    }
    return dataset;
}

// ---------------------------------------------------------------- 边界

namespace {

void includePoint(RangeD& range, bool& hasValue, double value) {
    if (!hasValue) {
        range = RangeD::FromValue(value);
        hasValue = true;
        return;
    }
    range.expandToInclude(value);
}

}  // namespace

DataBounds computeBounds(const Dataset& dataset, BarMode mode) {
    DataBounds bounds;

    if (mode == BarMode::Stacked) {
        const std::size_t count = dataset.categoryCount();
        if (count > 0) {
            bounds.x = RangeD{-0.5, static_cast<double>(count) - 0.5};
            bounds.hasData = true;
        }

        std::vector<double> positive(count, 0.0);
        std::vector<double> negative(count, 0.0);
        for (const Series& one : dataset.series) {
            const std::size_t n = std::min(one.points.size(), count);
            for (std::size_t i = 0; i < n; ++i) {
                const DataPoint& point = one.points[i];
                if (!point.valid()) {
                    continue;
                }
                if (point.y >= 0.0) {
                    positive[i] += point.y;
                } else {
                    negative[i] += point.y;
                }
            }
        }

        bool hasValue = false;
        RangeD yRange{0.0, 0.0};
        for (std::size_t i = 0; i < count; ++i) {
            includePoint(yRange, hasValue, positive[i]);
            includePoint(yRange, hasValue, negative[i]);
        }

        // 柱状图的零基线永远要画出来。
        includePoint(yRange, hasValue, 0.0);
        if (hasValue) {
            bounds.y = yRange;
        }
        return bounds;
    }

    if (mode == BarMode::Grouped) {
        const std::size_t count = dataset.categoryCount();
        if (count > 0) {
            bounds.x = RangeD{-0.5, static_cast<double>(count) - 0.5};
            bounds.hasData = true;
        }

        bool hasValue = false;
        RangeD yRange{0.0, 0.0};
        for (const Series& one : dataset.series) {
            for (const DataPoint& point : one.points) {
                if (point.valid()) {
                    includePoint(yRange, hasValue, point.y);
                }
            }
        }
        includePoint(yRange, hasValue, 0.0);
        if (hasValue) {
            bounds.y = yRange;
        }
        return bounds;
    }

    // 折线：x / y 都取所有有效点的外接范围。
    bool hasX = false;
    bool hasY = false;
    RangeD xRange{0.0, 0.0};
    RangeD yRange{0.0, 0.0};
    for (const Series& one : dataset.series) {
        for (const DataPoint& point : one.points) {
            if (!point.valid()) {
                continue;
            }
            includePoint(xRange, hasX, point.x);
            includePoint(yRange, hasY, point.y);
        }
    }

    if (hasX && hasY) {
        bounds.x = xRange;
        bounds.y = yRange;
        bounds.hasData = true;
    }
    return bounds;
}

// ---------------------------------------------------------------- 饼图

void PieChartData::sanitize(DataIssues* issues) {
    DataIssues local;
    for (PieSlice& slice : slices) {
        if (!std::isfinite(slice.value)) {
            slice.value = 0.0;
            ++local.nonFiniteValues;
            continue;
        }
        if (slice.value < 0.0) {
            // 饼图表达的是占比，负值没有几何意义。
            slice.value = 0.0;
            ++local.negativeValues;
        }
    }
    if (issues != nullptr) {
        issues->merge(local);
    }
}

double PieChartData::total() const noexcept {
    double sum = 0.0;
    for (const PieSlice& slice : slices) {
        if (std::isfinite(slice.value) && slice.value > 0.0) {
            sum += slice.value;
        }
    }
    return sum;
}

std::vector<double> PieChartData::fractions() const {
    std::vector<double> result(slices.size(), 0.0);
    const double sum = total();
    if (!(sum > 0.0)) {
        return result;
    }
    for (std::size_t i = 0; i < slices.size(); ++i) {
        const double value = slices[i].value;
        if (std::isfinite(value) && value > 0.0) {
            result[i] = value / sum;
        }
    }
    return result;
}

std::size_t PieChartData::usableCount() const noexcept {
    std::size_t count = 0;
    for (const PieSlice& slice : slices) {
        if (std::isfinite(slice.value) && slice.value > 0.0) {
            ++count;
        }
    }
    return count;
}

}  // namespace kiriyama::core
