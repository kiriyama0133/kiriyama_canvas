#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "kiriyama/core/Data.h"

namespace {

using kiriyama::core::BarMode;
using kiriyama::core::DataBounds;
using kiriyama::core::DataIssues;
using kiriyama::core::DataPoint;
using kiriyama::core::Dataset;
using kiriyama::core::PieChartData;
using kiriyama::core::Series;

kiriyama::core::Series makeSeries(std::initializer_list<DataPoint> points) {
    kiriyama::core::Series series;
    series.name = "s";
    series.points.assign(points.begin(), points.end());
    return series;
}

}  // namespace

TEST(DataModel, PointValidity) {
    EXPECT_TRUE(DataPoint::Value(1.0, 2.0).valid());
    EXPECT_FALSE(DataPoint::Null(1.0).valid());

    // 非有限值即使 hasValue 也算不可用
    EXPECT_FALSE(DataPoint::Value(1.0, std::nan("")).valid());
    EXPECT_FALSE(DataPoint::Value(std::numeric_limits<double>::infinity(), 1.0).valid());
}

TEST(DataModel, SeriesCountsValidPoints) {
    Series series;
    series.addPoint(0.0, 1.0);
    series.addNull(1.0);
    series.addPoint(2.0, 3.0);

    EXPECT_EQ(series.points.size(), 3u);
    EXPECT_EQ(series.validCount(), 2u);
}

TEST(DataModel, SanitizeTurnsNonFiniteIntoNull) {
    Dataset dataset;
    dataset.series.push_back(makeSeries(
        {DataPoint::Value(0.0, 1.0), DataPoint::Value(1.0, std::nan("")),
         DataPoint::Value(2.0, std::numeric_limits<double>::infinity()),
         DataPoint::Null(3.0)}));

    DataIssues issues;
    dataset.sanitize(&issues);

    EXPECT_EQ(issues.nonFiniteValues, 2u);
    EXPECT_EQ(issues.nullValues, 1u);
    EXPECT_FALSE(dataset.series[0].points[1].hasValue);
    EXPECT_FALSE(dataset.series[0].points[2].hasValue);
    EXPECT_EQ(dataset.validCount(), 1u);

    // 重复清洗不应该重复计数非有限值（已经变成 null 了）
    DataIssues second;
    dataset.sanitize(&second);
    EXPECT_EQ(second.nonFiniteValues, 0u);
    EXPECT_EQ(second.nullValues, 3u);
}

TEST(DataModel, EmptyDatasetHasNoBounds) {
    const Dataset empty;
    const DataBounds bounds = kiriyama::core::computeBounds(empty);
    EXPECT_FALSE(bounds.hasData);
    EXPECT_EQ(empty.categoryCount(), 0u);
}

TEST(DataModel, LineBoundsIgnoreGaps) {
    Dataset dataset;
    dataset.series.push_back(makeSeries({DataPoint::Value(0.0, 10.0), DataPoint::Null(1.0),
                                         DataPoint::Value(2.0, -5.0)}));
    dataset.series.push_back(makeSeries({DataPoint::Value(4.0, 7.0)}));

    const DataBounds bounds = kiriyama::core::computeBounds(dataset);
    ASSERT_TRUE(bounds.hasData);
    EXPECT_DOUBLE_EQ(bounds.x.minimum(), 0.0);
    EXPECT_DOUBLE_EQ(bounds.x.maximum(), 4.0);
    EXPECT_DOUBLE_EQ(bounds.y.minimum(), -5.0);
    EXPECT_DOUBLE_EQ(bounds.y.maximum(), 10.0);
}

TEST(DataModel, BarBoundsUseCategoryBandAndZeroBaseline) {
    Dataset dataset;
    dataset.categories = {"A", "B", "C"};
    dataset.series.push_back(makeSeries({DataPoint::Value(0.0, 5.0), DataPoint::Value(1.0, -2.0),
                                         DataPoint::Value(2.0, 0.0)}));

    const DataBounds bounds = kiriyama::core::computeBounds(dataset, BarMode::Grouped);
    ASSERT_TRUE(bounds.hasData);
    EXPECT_DOUBLE_EQ(bounds.x.minimum(), -0.5);
    EXPECT_DOUBLE_EQ(bounds.x.maximum(), 2.5);
    EXPECT_DOUBLE_EQ(bounds.y.minimum(), -2.0);
    EXPECT_DOUBLE_EQ(bounds.y.maximum(), 5.0);
}

TEST(DataModel, StackedBoundsSumPerCategory) {
    Dataset dataset;
    dataset.categories = {"A", "B"};
    dataset.series.push_back(makeSeries({DataPoint::Value(0.0, 1.0), DataPoint::Value(1.0, 2.0)}));
    dataset.series.push_back(makeSeries({DataPoint::Value(0.0, 3.0), DataPoint::Value(1.0, -1.0)}));

    const DataBounds bounds = kiriyama::core::computeBounds(dataset, BarMode::Stacked);
    ASSERT_TRUE(bounds.hasData);
    // A 类累计 1 + 3 = 4；B 类正负分开：+2 / -1
    EXPECT_DOUBLE_EQ(bounds.y.maximum(), 4.0);
    EXPECT_DOUBLE_EQ(bounds.y.minimum(), -1.0);
}

TEST(DataModel, PieFractionsSkipInvalidSlices) {
    PieChartData data;
    data.slices = {{"a", 1.0}, {"b", 2.0}, {"c", -3.0}, {"d", std::nan("")}, {"e", 0.0}};

    DataIssues issues;
    data.sanitize(&issues);

    EXPECT_EQ(issues.negativeValues, 1u);
    EXPECT_EQ(issues.nonFiniteValues, 1u);
    EXPECT_DOUBLE_EQ(data.total(), 3.0);
    EXPECT_EQ(data.usableCount(), 2u);

    const auto fractions = data.fractions();
    ASSERT_EQ(fractions.size(), 5u);
    EXPECT_NEAR(fractions[0], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(fractions[1], 2.0 / 3.0, 1e-12);
    EXPECT_DOUBLE_EQ(fractions[2], 0.0);
    EXPECT_DOUBLE_EQ(fractions[4], 0.0);
}

TEST(DataModel, PieEmptyTotalIsSafe) {
    PieChartData data;
    data.slices = {{"a", 0.0}, {"b", -1.0}};
    EXPECT_DOUBLE_EQ(data.total(), 0.0);
    const auto fractions = data.fractions();
    EXPECT_EQ(fractions.size(), 2u);
    EXPECT_DOUBLE_EQ(fractions[0], 0.0);
    EXPECT_DOUBLE_EQ(fractions[1], 0.0);
}

TEST(DataModel, CategoryDatasetFromRawTable) {
    std::vector<kiriyama::core::RawSeries> raw;
    raw.push_back({"revenue", {1.0, std::nullopt, std::nan(""), 4.0}});
    raw.push_back({"cost", {0.5, 0.25}});

    DataIssues issues;
    const Dataset dataset =
        kiriyama::core::makeCategoryDataset({"Q1", "Q2", "Q3"}, std::move(raw), &issues);

    EXPECT_EQ(issues.nullValues, 1u);
    EXPECT_EQ(issues.nonFiniteValues, 1u);

    // 第一列有 4 个值，类目表自动补到 4 列；第二列只有 2 个点，不补空点
    ASSERT_EQ(dataset.categories.size(), 4u);
    ASSERT_EQ(dataset.series.size(), 2u);
    EXPECT_EQ(dataset.series[0].points.size(), 4u);
    EXPECT_EQ(dataset.series[1].points.size(), 2u);

    EXPECT_FALSE(dataset.series[0].points[1].valid());
    EXPECT_FALSE(dataset.series[0].points[2].valid());
    EXPECT_TRUE(dataset.series[0].points[3].valid());

    const DataBounds bounds = kiriyama::core::computeBounds(dataset, BarMode::Grouped);
    ASSERT_TRUE(bounds.hasData);
    EXPECT_DOUBLE_EQ(bounds.x.minimum(), -0.5);
    EXPECT_DOUBLE_EQ(bounds.x.maximum(), 3.5);  // 最长列有 4 个点，类目自动补齐
    EXPECT_DOUBLE_EQ(bounds.y.minimum(), 0.0);
    EXPECT_DOUBLE_EQ(bounds.y.maximum(), 4.0);
}
