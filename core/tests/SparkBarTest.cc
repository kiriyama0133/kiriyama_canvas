#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "kiriyama/core/SparkBar.h"

namespace {

using kiriyama::core::computeSparkBarLayout;
using kiriyama::core::SparkBarData;
using kiriyama::core::SparkBarLayout;
using kiriyama::core::SparkBarOptions;
using kiriyama::core::Vec2d;

[[nodiscard]] SparkBarData sample() {
    SparkBarData data;
    data.series = {{"Revenue", kiriyama::core::Color::Hex(0x3E6DB5)},
                   {"Cost", kiriyama::core::Color::Hex(0xE0A050)}};
    data.tags = {"2022", "2023", "2024"};
    data.values = {
        {10.0, 4.0},
        {8.0, 6.0},
        {12.0, 2.0},
    };
    return data;
}

// ---------------------------------------------------------------- 数据

TEST(SparkBarData, TotalsSkipMissingAndNegative) {
    SparkBarData data;
    data.series = {{"a", {}}, {"b", {}}, {"c", {}}};
    data.tags = {"t0"};
    data.values = {{5.0, std::numeric_limits<double>::quiet_NaN(), -3.0}};

    EXPECT_DOUBLE_EQ(data.totalAt(0), 5.0);
    EXPECT_DOUBLE_EQ(data.valueAt(0, 1), 0.0);   // NaN
    EXPECT_DOUBLE_EQ(data.valueAt(0, 2), 0.0);   // 负值
    EXPECT_DOUBLE_EQ(data.valueAt(0, 9), 0.0);   // 越界
    EXPECT_DOUBLE_EQ(data.valueAt(9, 0), 0.0);   // 行越界
}

TEST(SparkBarData, MaxTotalPicksTallest) {
    SparkBarData data = sample();
    EXPECT_DOUBLE_EQ(data.maxTotal(), 14.0);  // 2022: 14, 2023: 14, 2024: 14
    data.values[0][0] = 20.0;
    EXPECT_DOUBLE_EQ(data.maxTotal(), 24.0);
}

TEST(SparkBarData, EmptyIsEmpty) {
    SparkBarData data;
    EXPECT_TRUE(data.empty());
    data.tags = {"only tag"};
    EXPECT_TRUE(data.empty());  // 没有 series 也算空
}

// ---------------------------------------------------------------- 布局

TEST(SparkBarLayout, OneItemPerTag) {
    const SparkBarData data = sample();
    SparkBarOptions options;
    options.showTags = false;
    options.barGap = 10.0;

    const SparkBarLayout layout = computeSparkBarLayout(Vec2d{200.0, 100.0}, data, options);
    ASSERT_TRUE(layout.valid);
    ASSERT_EQ(layout.items.size(), 3u);

    // 两条 series 都有值 → 每柱两段。
    for (const auto& item : layout.items) {
        EXPECT_EQ(item.segments.size(), 2u);
    }
}

TEST(SparkBarLayout, SegmentsStackBottomUp) {
    SparkBarData data;
    data.series = {{"a", {}}, {"b", {}}};
    data.tags = {"t"};
    data.values = {{3.0, 1.0}};

    SparkBarOptions options;
    options.showTags = false;

    const SparkBarLayout layout = computeSparkBarLayout(Vec2d{100.0, 100.0}, data, options);
    ASSERT_EQ(layout.items.size(), 1u);
    const auto& segments = layout.items[0].segments;
    ASSERT_EQ(segments.size(), 2u);

    // 第一段（series 0）在下面：底边贴着基线，顶边低于第二段的底边。
    EXPECT_NEAR(segments[0].rect.bottom(), 100.0, 1e-9);
    EXPECT_GT(segments[0].rect.top(), segments[1].rect.top());
    // 顶段的顶边与整柱外接框的顶边只差一个段缝隙（默认 1.5px）。
    EXPECT_LE(std::abs(segments[1].rect.top() - layout.items[0].barRect.top()), 2.0);

    // 高度按 3:1 分配（各段还有 1.5px 缝隙）。
    EXPECT_GT(segments[0].rect.height, segments[1].rect.height);
}

TEST(SparkBarLayout, ZeroTotalKeepsTagOnly) {
    SparkBarData data;
    data.series = {{"a", {}}};
    data.tags = {"empty"};
    data.values = {{0.0}};

    const SparkBarLayout layout =
        computeSparkBarLayout(Vec2d{80.0, 60.0}, data, SparkBarOptions{});
    ASSERT_EQ(layout.items.size(), 1u);
    EXPECT_TRUE(layout.items[0].segments.empty());
    EXPECT_FALSE(layout.items[0].tagRect.empty());  // 标签还在
}

TEST(SparkBarLayout, TagsSitBelowBars) {
    const SparkBarData data = sample();
    const SparkBarLayout layout = computeSparkBarLayout(Vec2d{200.0, 120.0}, data);
    ASSERT_TRUE(layout.valid);

    for (const auto& item : layout.items) {
        ASSERT_FALSE(item.tagRect.empty());
        EXPECT_GT(item.tagRect.top(), item.barRect.bottom());
    }
}

TEST(SparkBarLayout, ExplicitBarWidthAndAutoWidth) {
    const SparkBarData data = sample();

    SparkBarOptions fixed;
    fixed.showTags = false;
    fixed.barWidth = 20.0;
    fixed.barGap = 10.0;
    const SparkBarLayout fixedLayout = computeSparkBarLayout(Vec2d{300.0, 100.0}, data, fixed);
    ASSERT_EQ(fixedLayout.items.size(), 3u);
    EXPECT_DOUBLE_EQ(fixedLayout.items[0].barRect.width, 20.0);
    // 3 柱总宽 20*3 + 10*2 = 80，画布 300 → 水平居中。
    EXPECT_NEAR(fixedLayout.items[0].barRect.left(), (300.0 - 80.0) * 0.5, 1e-9);

    SparkBarOptions autoWidth;
    autoWidth.showTags = false;
    autoWidth.barGap = 10.0;
    const SparkBarLayout autoLayout = computeSparkBarLayout(Vec2d{80.0, 100.0}, data, autoWidth);
    // (80 - 20) / 3 = 20
    EXPECT_NEAR(autoLayout.items[0].barRect.width, 20.0, 1e-9);
}

TEST(SparkBarLayout, InvalidOnEmptyCanvasOrData) {
    SparkBarData data = sample();
    EXPECT_FALSE(computeSparkBarLayout(Vec2d{0.0, 100.0}, data).valid);
    EXPECT_FALSE(computeSparkBarLayout(Vec2d{100.0, 0.0}, data).valid);
    EXPECT_FALSE(computeSparkBarLayout(Vec2d{100.0, 100.0}, SparkBarData{}).valid);
}

}  // namespace
