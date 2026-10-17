#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kiriyama/core/Layout.h"

namespace {

using kiriyama::core::AxisOptions;
using kiriyama::core::DataBounds;
using kiriyama::core::Dataset;
using kiriyama::core::DataPoint;
using kiriyama::core::LayoutOptions;
using kiriyama::core::PieChartData;
using kiriyama::core::PieLayoutOptions;
using kiriyama::core::ScaleType;
using kiriyama::core::Series;
using kiriyama::core::YDirection;
using kiriyama::core::DataIssues;
using kiriyama::math::kPi;
using kiriyama::math::kTwoPi;

DataBounds makeBounds(double x0, double x1, double y0, double y1) {
    DataBounds bounds;
    bounds.x = {x0, x1};
    bounds.y = {y0, y1};
    bounds.hasData = true;
    return bounds;
}

LayoutOptions makeOptions() {
    LayoutOptions options;
    options.padding = kiriyama::core::Padding::All(16.0);
    return options;
}

}  // namespace

TEST(Layout, BasicPlotAreaInsideCanvas) {
    const auto layout = kiriyama::core::computeLayout({800.0, 600.0}, makeBounds(0.0, 10.0, 0.0, 100.0),
                                                      makeOptions());

    ASSERT_TRUE(layout.valid);
    EXPECT_GE(layout.plot.left(), 16.0);
    EXPECT_GE(layout.plot.top(), 16.0);
    EXPECT_LE(layout.plot.right(), 800.0 - 16.0);
    EXPECT_LE(layout.plot.bottom(), 600.0 - 16.0);
    EXPECT_GT(layout.plot.width, 0.0);
    EXPECT_GT(layout.plot.height, 0.0);

    // y 轴刻度是 0 / 20 / ... / 100，共 6 个
    ASSERT_EQ(layout.yAxis.ticks.size(), 6u);
    EXPECT_DOUBLE_EQ(layout.yAxis.domain.minimum(), 0.0);
    EXPECT_DOUBLE_EQ(layout.yAxis.domain.maximum(), 100.0);
    EXPECT_DOUBLE_EQ(layout.yAxis.ticks.front().value, 0.0);
    EXPECT_DOUBLE_EQ(layout.yAxis.ticks.back().value, 100.0);

    EXPECT_EQ(layout.xAxis.ticks.size(), 6u);
    EXPECT_NEAR(layout.xAxis.ticks.front().position, layout.plot.left(), 1e-9);
    EXPECT_NEAR(layout.xAxis.ticks.back().position, layout.plot.right(), 1e-9);
}

TEST(Layout, YDirectionControlsOrigin) {
    auto down = makeOptions();
    down.yDirection = YDirection::Down;
    const auto screen = kiriyama::core::computeLayout({800.0, 600.0},
                                                      makeBounds(0.0, 10.0, 0.0, 100.0), down);

    auto up = makeOptions();
    up.yDirection = YDirection::Up;
    const auto math = kiriyama::core::computeLayout({800.0, 600.0},
                                                    makeBounds(0.0, 10.0, 0.0, 100.0), up);

    // 左上角原点：0 在顶部
    EXPECT_NEAR(screen.toPixelY(0.0), screen.plot.top(), 1e-9);
    EXPECT_NEAR(screen.toPixelY(100.0), screen.plot.bottom(), 1e-9);

    // 左下角原点：0 在底部
    EXPECT_NEAR(math.toPixelY(0.0), math.plot.bottom(), 1e-9);
    EXPECT_NEAR(math.toPixelY(100.0), math.plot.top(), 1e-9);
}

TEST(Layout, CoordinateRoundTrip) {
    const auto layout = kiriyama::core::computeLayout({640.0, 480.0},
                                                      makeBounds(-5.0, 15.0, -20.0, 80.0),
                                                      makeOptions());
    ASSERT_TRUE(layout.valid);

    const kiriyama::math::Vec2d dataPoint{3.25, 41.5};
    const auto pixel = layout.toPixel(dataPoint);
    const auto restored = layout.fromPixel(pixel);

    EXPECT_NEAR(restored.x, dataPoint.x, 1e-9);
    EXPECT_NEAR(restored.y, dataPoint.y, 1e-9);
    EXPECT_TRUE(layout.plot.contains(pixel));
}

TEST(Layout, NiceDomainSnapsToCleanSteps) {
    const auto layout = kiriyama::core::computeLayout({800.0, 600.0},
                                                      makeBounds(0.0, 10.0, 3.0, 97.0),
                                                      makeOptions());

    ASSERT_TRUE(layout.valid);
    EXPECT_DOUBLE_EQ(layout.yAxis.domain.minimum(), 0.0);
    EXPECT_DOUBLE_EQ(layout.yAxis.domain.maximum(), 100.0);

    ASSERT_GE(layout.yAxis.ticks.size(), 2u);
    const double step = layout.yAxis.ticks[1].value - layout.yAxis.ticks[0].value;
    EXPECT_DOUBLE_EQ(step, 20.0);
}

TEST(Layout, SingleValueDomainGetsPadding) {
    const auto layout = kiriyama::core::computeLayout({400.0, 300.0},
                                                      makeBounds(5.0, 5.0, 42.0, 42.0),
                                                      makeOptions());
    ASSERT_TRUE(layout.valid);
    EXPECT_LT(layout.yAxis.domain.minimum(), 42.0);
    EXPECT_GT(layout.yAxis.domain.maximum(), 42.0);
    EXPECT_LT(layout.xAxis.domain.minimum(), 5.0);
    EXPECT_GT(layout.xAxis.domain.maximum(), 5.0);
}

TEST(Layout, MultipleAxesShareGridlines) {
    auto options = makeOptions();
    options.hasSecondaryY = true;

    const auto layout = kiriyama::core::computeLayout({800.0, 600.0},
                                                      makeBounds(0.0, 10.0, 0.0, 100.0),
                                                      makeBounds(0.0, 10.0, 0.0, 7.0), options);

    ASSERT_TRUE(layout.valid);
    ASSERT_TRUE(layout.hasSecondaryY);
    ASSERT_FALSE(layout.yAxis.ticks.empty());
    EXPECT_EQ(layout.yAxis.ticks.size(), layout.secondaryYAxis.ticks.size());

    // 两轴分段数相同，刻度像素位置逐条重合
    for (std::size_t i = 0; i < layout.yAxis.ticks.size(); ++i) {
        EXPECT_NEAR(layout.yAxis.ticks[i].position, layout.secondaryYAxis.ticks[i].position,
                    1e-6);
    }

    // 右轴被对齐到 nice 的 0..10，而不是 0..7
    EXPECT_DOUBLE_EQ(layout.secondaryYAxis.domain.minimum(), 0.0);
    EXPECT_DOUBLE_EQ(layout.secondaryYAxis.domain.maximum(), 10.0);
    EXPECT_LT(layout.plot.right(), 800.0 - 16.0);
}

TEST(Layout, AlignedDomainsCoverOriginalRanges) {
    const auto aligned = kiriyama::core::alignYDomains({3.0, 91.0}, {-4.0, 33.0}, 5);
    EXPECT_EQ(aligned.intervals, 5);
    EXPECT_LE(aligned.primary.minimum(), 3.0);
    EXPECT_GE(aligned.primary.maximum(), 91.0);
    EXPECT_LE(aligned.secondary.minimum(), -4.0);
    EXPECT_GE(aligned.secondary.maximum(), 33.0);
    // [3, 91] 用 step 20 × 5 段覆盖，居中后是 [-3, 103]
    EXPECT_NEAR(aligned.primary.span(), 100.0, 1e-9);
}

TEST(Layout, LogAxisMapsByMagnitude) {
    auto options = makeOptions();
    options.y.scale = ScaleType::Log;

    const auto layout = kiriyama::core::computeLayout({800.0, 600.0},
                                                      makeBounds(0.0, 10.0, 1.0, 1.0e6), options);
    ASSERT_TRUE(layout.valid);
    EXPECT_EQ(layout.yAxis.scale, ScaleType::Log);
    EXPECT_DOUBLE_EQ(layout.yAxis.domain.minimum(), 1.0);
    EXPECT_DOUBLE_EQ(layout.yAxis.domain.maximum(), 1.0e6);

    // 十年跨度：1, 100, 10k, 1M
    ASSERT_EQ(layout.yAxis.ticks.size(), 4u);
    EXPECT_NEAR(layout.yAxis.ticks[0].value, 1.0, 1e-9);
    EXPECT_NEAR(layout.yAxis.ticks[3].value, 1.0e6, 1e-3);

    // 1000 正好在对数中点
    EXPECT_NEAR(layout.toPixelY(1000.0), layout.plot.center().y, 1e-6);
    // 默认左上角原点：小值在顶部，大值在底部
    EXPECT_NEAR(layout.toPixelY(1.0), layout.plot.top(), 1e-6);
    EXPECT_NEAR(layout.toPixelY(1.0e6), layout.plot.bottom(), 1e-6);
}

TEST(Layout, LogDomainHandlesNonPositiveData) {
    const auto domain = kiriyama::core::niceLogDomain({0.0, 1000.0});
    EXPECT_GT(domain.minimum(), 0.0);
    EXPECT_GE(domain.maximum(), 1000.0);
    EXPECT_GE(domain.maximum() / domain.minimum(), 1000.0);
}

TEST(Layout, CategoryAxisUsesLabelsAndBandDomain) {
    Dataset dataset;
    dataset.categories = {"Q1", "Q2", "Q3", "Q4"};
    for (int i = 0; i < 4; ++i) {
        Series series;
        series.name = "revenue";
        for (int j = 0; j < 4; ++j) {
            series.addPoint(j, static_cast<double>(j + 1));
        }
        dataset.series.push_back(series);
    }

    auto options = makeOptions();
    options.categoryLabels = dataset.categories;

    const auto layout = kiriyama::core::computeLayout({600.0, 400.0},
                                                      kiriyama::core::computeBounds(
                                                          dataset, kiriyama::core::BarMode::Grouped),
                                                      options);
    ASSERT_TRUE(layout.valid);
    EXPECT_DOUBLE_EQ(layout.xAxis.domain.minimum(), -0.5);
    EXPECT_DOUBLE_EQ(layout.xAxis.domain.maximum(), 3.5);
    ASSERT_EQ(layout.xAxis.ticks.size(), 4u);
    EXPECT_EQ(layout.xAxis.ticks[0].label, "Q1");
    EXPECT_EQ(layout.xAxis.ticks[3].label, "Q4");

    // 类别中心落在等分点上
    EXPECT_NEAR(layout.toPixelX(0.0), layout.plot.left() + layout.plot.width * 0.125, 1e-9);
}

TEST(Layout, OverlapAvoidanceReducesXTicks) {
    auto options = makeOptions();
    options.measureText = [](std::string_view text, double fontSize) {
        return kiriyama::core::TextMetrics{80.0, fontSize * 1.25};
    };

    const auto layout = kiriyama::core::computeLayout({400.0, 300.0},
                                                      makeBounds(0.0, 10.0, 0.0, 100.0), options);
    ASSERT_TRUE(layout.valid);
    EXPECT_LT(layout.xAxis.ticks.size(), 6u);
    EXPECT_GE(layout.xAxis.ticks.size(), 2u);
}

TEST(Layout, SmallCanvasIsInvalidButSafe) {
    const auto layout = kiriyama::core::computeLayout({40.0, 30.0},
                                                      makeBounds(0.0, 10.0, 0.0, 10.0),
                                                      makeOptions());
    EXPECT_FALSE(layout.valid);
    EXPECT_GE(layout.plot.width, 0.0);
    EXPECT_GE(layout.plot.height, 0.0);
}

TEST(Layout, EmptyBoundsFallBackToUnitRange) {
    const auto layout = kiriyama::core::computeLayout({400.0, 300.0}, DataBounds{}, makeOptions());
    EXPECT_TRUE(layout.valid);
    EXPECT_FALSE(layout.yAxis.ticks.empty());
    EXPECT_TRUE(std::isfinite(layout.yAxis.domain.minimum()));
    EXPECT_TRUE(std::isfinite(layout.yAxis.domain.maximum()));
}

TEST(Layout, DefaultTextMeasureCountsCjkAsFullWidth) {
    const auto ascii = kiriyama::core::defaultTextMeasure("123", 10.0);
    const auto cjk = kiriyama::core::defaultTextMeasure("一二三", 10.0);
    EXPECT_GT(cjk.width, ascii.width);
    EXPECT_NEAR(ascii.height, 12.5, 1e-9);
}

TEST(Layout, PieSectorsCoverFullCircle) {
    PieChartData data;
    data.slices = {{"a", 1.0}, {"b", 2.0}, {"c", 3.0}};
    DataIssues issues;
    data.sanitize(&issues);

    PieLayoutOptions options;
    options.padding = kiriyama::core::Padding::All(16.0);

    const auto layout = kiriyama::core::computePieLayout({400.0, 400.0}, data, options);
    ASSERT_TRUE(layout.valid);
    ASSERT_EQ(layout.sectors.size(), 3u);

    EXPECT_NEAR(layout.center.x, 200.0, 1e-9);
    EXPECT_NEAR(layout.center.y, 200.0, 1e-9);
    EXPECT_NEAR(layout.outerRadius, 184.0 * 0.85, 1e-9);
    EXPECT_DOUBLE_EQ(layout.innerRadius, 0.0);

    EXPECT_NEAR(layout.sectors[0].fraction, 1.0 / 6.0, 1e-12);
    EXPECT_NEAR(layout.sectors[1].fraction, 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(layout.sectors[2].fraction, 0.5, 1e-12);

    double totalSweep = 0.0;
    for (const auto& sector : layout.sectors) {
        totalSweep += sector.sweepAngle;
    }
    EXPECT_NEAR(totalSweep, kTwoPi, 1e-9);
    EXPECT_NEAR(layout.sectors[0].startAngle, -kPi * 0.5, 1e-9);

    // 扇区之间角度连续
    EXPECT_NEAR(layout.sectors[0].startAngle + layout.sectors[0].sweepAngle,
                layout.sectors[1].startAngle, 1e-9);
}

TEST(Layout, PieWithNoPositiveValueIsInvalid) {
    PieChartData data;
    data.slices = {{"a", 0.0}, {"b", -2.0}};
    const auto layout = kiriyama::core::computePieLayout({400.0, 400.0}, data);
    EXPECT_FALSE(layout.valid);
    EXPECT_TRUE(layout.sectors.empty());
}
