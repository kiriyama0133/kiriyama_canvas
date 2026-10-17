#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "kiriyama/core/Chart.h"

namespace {

using kiriyama::core::ArcD;
using kiriyama::core::BarMode;
using kiriyama::core::Dataset;
using kiriyama::core::DataPoint;
using kiriyama::core::HitTestOptions;
using kiriyama::core::HitResult;
using kiriyama::core::LayoutOptions;
using kiriyama::core::PieChartData;
using kiriyama::core::PieLayoutOptions;
using kiriyama::core::RectD;
using kiriyama::core::Series;
using kiriyama::math::Vec2d;
using kiriyama::math::kHalfPi;
using kiriyama::math::kPi;

Dataset makeCartesianDataset() {
    Dataset dataset;
    dataset.categories = {"A", "B", "C", "D"};
    Series series;
    series.name = "s0";
    for (int i = 0; i < 4; ++i) {
        series.addPoint(i, static_cast<double>((i + 1) * 10));
    }
    dataset.series.push_back(series);
    return dataset;
}

kiriyama::core::ChartLayout makeLayout(const Dataset& dataset, kiriyama::core::BarMode mode) {
    LayoutOptions options;
    options.padding = kiriyama::core::Padding::All(16.0);
    options.categoryLabels = dataset.categories;
    return kiriyama::core::computeLayout(
        {600.0, 400.0}, kiriyama::core::computeBounds(dataset, mode), options);
}

}  // namespace

// ---------------------------------------------------------------- 几何

TEST(Geometry, RectBasics) {
    const RectD rect = RectD::FromLTRB(10.0, 20.0, 110.0, 70.0);
    EXPECT_DOUBLE_EQ(rect.width, 100.0);
    EXPECT_DOUBLE_EQ(rect.height, 50.0);
    EXPECT_TRUE(rect.contains({10.0, 20.0}));
    EXPECT_TRUE(rect.contains({60.0, 45.0}));
    EXPECT_FALSE(rect.contains({111.0, 45.0}));
    EXPECT_FALSE(rect.empty());

    const RectD overlapping = RectD::FromLTRB(100.0, 60.0, 200.0, 120.0);
    EXPECT_TRUE(rect.intersects(overlapping));
    const RectD cut = rect.intersection(overlapping);
    EXPECT_DOUBLE_EQ(cut.left(), 100.0);
    EXPECT_DOUBLE_EQ(cut.top(), 60.0);
    EXPECT_DOUBLE_EQ(cut.right(), 110.0);
    EXPECT_DOUBLE_EQ(cut.bottom(), 70.0);

    // 仅共边不算重叠
    const RectD touching = RectD::FromLTRB(110.0, 20.0, 150.0, 70.0);
    EXPECT_FALSE(rect.intersects(touching));

    // 内缩到负尺寸时塌缩而不是翻转
    const RectD collapsed = rect.inset(80.0);
    EXPECT_DOUBLE_EQ(collapsed.width, 0.0);
    EXPECT_DOUBLE_EQ(collapsed.height, 0.0);
}

TEST(Geometry, SegmentDistanceAndClamping) {
    const Vec2d a{0.0, 0.0};
    const Vec2d b{10.0, 0.0};

    EXPECT_DOUBLE_EQ(kiriyama::core::distanceToSegment({5.0, 5.0}, a, b), 5.0);
    EXPECT_DOUBLE_EQ(kiriyama::core::distanceToSegment({5.0, 0.0}, a, b), 0.0);

    // 投影落在线段外，按端点算
    EXPECT_DOUBLE_EQ(kiriyama::core::distanceToSegment({-5.0, 0.0}, a, b), 5.0);
    EXPECT_DOUBLE_EQ(kiriyama::core::distanceToSegment({15.0, 0.0}, a, b), 5.0);

    const Vec2d closest = kiriyama::core::closestPointOnSegment({-4.0, 3.0}, a, b);
    EXPECT_NEAR(closest.x, 0.0, 1e-12);
    EXPECT_NEAR(closest.y, 0.0, 1e-12);

    // 退化线段
    EXPECT_DOUBLE_EQ(kiriyama::core::distanceToSegment({3.0, 4.0}, a, a), 5.0);
}

TEST(Geometry, ArcContainsAngleAndRadius) {
    const ArcD arc{{0.0, 0.0}, 2.0, 10.0, -kHalfPi, kHalfPi};

    EXPECT_TRUE(arc.containsAngle(-kHalfPi * 0.5));
    EXPECT_TRUE(arc.containsAngle(0.0));
    EXPECT_FALSE(arc.containsAngle(kHalfPi * 0.5));
    EXPECT_FALSE(arc.containsAngle(-kPi));

    EXPECT_TRUE(arc.contains({5.0, 0.0}));
    EXPECT_FALSE(arc.contains({0.0, 0.0}));    // 内孔
    EXPECT_FALSE(arc.contains({20.0, 0.0}));   // 超出外半径
    EXPECT_FALSE(arc.contains({-5.0, 0.0}));   // 角度不在扇区
}

TEST(Geometry, LabelCollisionGreedyKeep) {
    const std::vector<RectD> boxes = {
        RectD::FromLTRB(0.0, 0.0, 50.0, 20.0),
        RectD::FromLTRB(10.0, 5.0, 60.0, 25.0),
        RectD::FromLTRB(100.0, 0.0, 150.0, 20.0),
        RectD{0.0, 0.0, 0.0, 0.0},
    };

    const std::vector<bool> visible = kiriyama::core::resolveLabelOverlaps(boxes, 2.0);
    ASSERT_EQ(visible.size(), 4u);
    EXPECT_TRUE(visible[0]);
    EXPECT_FALSE(visible[1]);  // 与第一个重叠
    EXPECT_TRUE(visible[2]);
    EXPECT_TRUE(visible[3]);  // 空标签不占位
}

// ---------------------------------------------------------------- 折线

TEST(HitTest, PolylineHitsSegmentAndSnapsToVertex) {
    Dataset dataset;
    Series series;
    series.addPoint(0.0, 5.0);
    series.addPoint(10.0, 5.0);
    dataset.series.push_back(series);

    LayoutOptions options;
    const auto layout = kiriyama::core::computeLayout(
        {800.0, 600.0}, kiriyama::core::computeBounds(dataset), options);

    const Vec2d target = layout.toPixel(0.2, 5.0);
    const HitResult hit = kiriyama::core::hitTestPolyline(dataset, layout, target);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.seriesIndex, 0u);
    EXPECT_EQ(hit.pointIndex, 0u);
    EXPECT_LT(hit.distance, 1e-6);
    EXPECT_NEAR(hit.position.x, layout.toPixelX(0.0), 1e-6);
    EXPECT_DOUBLE_EQ(hit.value, 5.0);

    // 垂直偏移 20px 不该命中（默认容差 6px）
    const HitResult miss =
        kiriyama::core::hitTestPolyline(dataset, layout, target + Vec2d{0.0, 20.0});
    EXPECT_FALSE(miss.hit);
}

TEST(HitTest, NullValueBreaksTheLine) {
    Dataset gapped;
    Series series;
    series.addPoint(0.0, 0.0);
    series.addNull(5.0);
    series.addPoint(10.0, 0.0);
    gapped.series.push_back(series);

    LayoutOptions options;
    const auto layout = kiriyama::core::computeLayout(
        {800.0, 600.0}, kiriyama::core::computeBounds(gapped), options);

    const Vec2d midpoint = layout.toPixel(5.0, 0.0);
    EXPECT_FALSE(kiriyama::core::hitTestPolyline(gapped, layout, midpoint).hit);

    // 同样的数据没有空值时应该命中
    Dataset solid;
    Series solidSeries;
    solidSeries.addPoint(0.0, 0.0);
    solidSeries.addPoint(5.0, 0.0);
    solidSeries.addPoint(10.0, 0.0);
    solid.series.push_back(solidSeries);
    const auto solidLayout = kiriyama::core::computeLayout(
        {800.0, 600.0}, kiriyama::core::computeBounds(solid), options);
    EXPECT_TRUE(kiriyama::core::hitTestPolyline(
                    solid, solidLayout, solidLayout.toPixel(5.0, 0.0))
                    .hit);
}

TEST(HitTest, VerticesIgnoreSegments) {
    Dataset dataset;
    Series series;
    series.addPoint(0.0, 0.0);
    series.addPoint(10.0, 10.0);
    dataset.series.push_back(series);

    const auto layout = kiriyama::core::computeLayout(
        {800.0, 600.0}, kiriyama::core::computeBounds(dataset));

    // 线段中点：折线命中，顶点不命中
    const Vec2d middle = layout.toPixel(5.0, 5.0);
    EXPECT_TRUE(kiriyama::core::hitTestPolyline(dataset, layout, middle).hit);
    EXPECT_FALSE(kiriyama::core::hitTestVertices(dataset, layout, middle).hit);

    // 顶点附近两者都命中
    const Vec2d vertex = layout.toPixel(10.0, 10.0);
    EXPECT_TRUE(kiriyama::core::hitTestVertices(dataset, layout, vertex).hit);
}

// ---------------------------------------------------------------- 柱状

TEST(HitTest, GroupedBarHit) {
    Dataset dataset = makeCartesianDataset();
    const auto layout = makeLayout(dataset, BarMode::Grouped);
    const auto rects = kiriyama::core::computeBarRects(dataset, layout, BarMode::Grouped);
    ASSERT_EQ(rects.size(), 4u);

    for (const auto& bar : rects) {
        const HitResult hit =
            kiriyama::core::hitTestBars(dataset, layout, BarMode::Grouped, bar.rect.center());
        ASSERT_TRUE(hit.hit);
        EXPECT_EQ(hit.seriesIndex, bar.seriesIndex);
        EXPECT_EQ(hit.categoryIndex, bar.categoryIndex);
        EXPECT_DOUBLE_EQ(hit.value, bar.value);
    }

    // 绘图区外不命中
    EXPECT_FALSE(kiriyama::core::hitTestBars(dataset, layout, BarMode::Grouped, {-10.0, -10.0}).hit);
}

TEST(HitTest, StackedBarHitUpperSeries) {
    Dataset dataset;
    dataset.categories = {"A"};
    Series lower;
    lower.addPoint(0, 3.0);
    Series upper;
    upper.addPoint(0, 4.0);
    dataset.series = {lower, upper};

    const auto layout = makeLayout(dataset, BarMode::Stacked);
    const auto rects = kiriyama::core::computeBarRects(dataset, layout, BarMode::Stacked);
    ASSERT_EQ(rects.size(), 2u);

    const Vec2d insideUpper{rects[1].rect.center().x, layout.toPixelY(5.0)};
    const HitResult hit =
        kiriyama::core::hitTestBars(dataset, layout, BarMode::Stacked, insideUpper);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.seriesIndex, 1u);
    EXPECT_DOUBLE_EQ(hit.value, 4.0);

    const Vec2d insideLower{rects[0].rect.center().x, layout.toPixelY(1.0)};
    EXPECT_EQ(kiriyama::core::hitTestBars(dataset, layout, BarMode::Stacked, insideLower)
                  .seriesIndex,
              0u);
}

// ---------------------------------------------------------------- 饼图

TEST(HitTest, PieSectorByAngle) {
    PieChartData data;
    data.slices = {{"a", 1.0}, {"b", 1.0}, {"c", 1.0}, {"d", 1.0}};

    PieLayoutOptions options;
    options.padding = kiriyama::core::Padding::All(10.0);
    const auto layout = kiriyama::core::computePieLayout({400.0, 400.0}, data, options);
    ASSERT_TRUE(layout.valid);

    const double radius = (layout.innerRadius + layout.outerRadius) * 0.5;
    const Vec2d at45 = layout.center + Vec2d{std::cos(kPi * 0.25), std::sin(kPi * 0.25)} * radius;
    const HitResult hit = kiriyama::core::hitTestPie(data, layout, at45);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.sliceIndex, 1u);
    EXPECT_NEAR(hit.fraction, 0.25, 1e-12);

    const Vec2d at225 =
        layout.center + Vec2d{std::cos(kPi * 1.25), std::sin(kPi * 1.25)} * radius;
    // 屏幕 y 向下，kPi*1.25 落在左上象限，属于 180°~270° 的最后一个扇区
    EXPECT_EQ(kiriyama::core::hitTestPie(data, layout, at225).sliceIndex, 3u);
}

TEST(HitTest, PieHoleAndOutsideDoNotHit) {
    PieChartData data;
    data.slices = {{"a", 1.0}, {"b", 1.0}};

    PieLayoutOptions options;
    options.innerRadiusRatio = 0.5;
    const auto layout = kiriyama::core::computePieLayout({400.0, 400.0}, data, options);
    ASSERT_TRUE(layout.valid);

    EXPECT_FALSE(kiriyama::core::hitTestPie(data, layout, layout.center).hit);
    EXPECT_FALSE(kiriyama::core::hitTestPie(
                     data, layout, {layout.center.x + layout.outerRadius + 30.0, layout.center.y})
                     .hit);
}

// ---------------------------------------------------------------- 类目

TEST(HitTest, NearestCategory) {
    Dataset dataset = makeCartesianDataset();
    const auto layout = makeLayout(dataset, BarMode::Grouped);

    const Vec2d pixel{layout.toPixelX(2.0), layout.plot.center().y};
    const HitResult hit = kiriyama::core::hitTestCategory(dataset, layout, pixel);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.categoryIndex, 2u);
    EXPECT_NEAR(hit.position.x, pixel.x, 1e-9);

    // 绘图区外不命中
    EXPECT_FALSE(kiriyama::core::hitTestCategory(dataset, layout, {0.0, 0.0}).hit);

    EXPECT_EQ(kiriyama::core::nearestCategoryIndex(dataset, 2.1), 2u);
    EXPECT_EQ(kiriyama::core::nearestCategoryIndex(dataset, -100.0), 0u);
    EXPECT_EQ(kiriyama::core::nearestCategoryIndex(Dataset{}, 0.0),
              kiriyama::core::kInvalidIndex);
}
