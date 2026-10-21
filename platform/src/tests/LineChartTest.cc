#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "kiriyama/core/SceneQuery.h"
#include "kiriyama/scene/LineChart.h"

namespace {

using kiriyama::core::Color;
using kiriyama::core::CursorShape;
using kiriyama::core::Dataset;
using kiriyama::core::findByKey;
using kiriyama::core::RectD;
using kiriyama::core::SceneNode;
using kiriyama::core::Series;
using kiriyama::core::TextDirection;
using kiriyama::core::Vec2d;
using kiriyama::scene::findHoveredTooltipTarget;
using kiriyama::scene::FrameContext;
using kiriyama::scene::LineChart;
using kiriyama::scene::LineChartConfig;
using kiriyama::scene::LinePointNode;
using kiriyama::scene::TooltipTarget;
using kiriyama::scene::TooltipText;

[[nodiscard]] FrameContext frameAt(Vec2d pos, bool down = false) {
    FrameContext ctx;
    ctx.time = 1.0;
    ctx.deltaTime = 0.016;
    ctx.pointer.position = pos;
    ctx.pointer.down = down;
    return ctx;
}

[[nodiscard]] Dataset makeSampleDataset() {
    Dataset ds;
    ds.categories = {"Q1", "Q2", "Q3", "Q4"};
    Series s;
    s.name = "Revenue";
    s.addPoint(0.0, 0.0);
    s.addPoint(1.0, 50.0);
    s.addPoint(2.0, 100.0);
    s.addPoint(3.0, 75.0);
    ds.series.push_back(std::move(s));
    return ds;
}

}  // namespace

TEST(LineChart, DualAxisCustomTextAndStyleDirectionConfig) {
    LineChart chart("line");
    chart.setFrame({0.0, 0.0, 640.0, 360.0});
    chart.setData(makeSampleDataset());

    LineChartConfig cfg;
    cfg.layout.x.tickLabels = {"Spring", "Summer", "Autumn", "Winter"};
    cfg.layout.x.labelFontSize = 13.0;
    cfg.layout.x.labelColor = Color::Hex(0x224488);
    cfg.layout.x.labelDirection = TextDirection::Vertical;
    cfg.layout.x.labelAngle = 0.35;  // 旋转偏移角（弧度）

    cfg.layout.y.tickLabels = {"Low", "Mid", "High"};
    cfg.layout.y.labelFontSize = 14.0;
    cfg.layout.y.labelColor = Color::Hex(0x884422);
    cfg.layout.y.labelDirection = TextDirection::Horizontal;
    cfg.layout.y.labelAngle = -0.20;
    chart.setConfig(cfg);

    chart.tickTree(frameAt({-100.0, -100.0}));
    ASSERT_TRUE(chart.valid());

    // X 轴与 Y 轴刻度都生成了配置的自定义 Text、颜色、方向与旋转矩阵
    const SceneNode* xTick0 = findByKey(chart, "axis.x.tick.0");
    const SceneNode* yTick0 = findByKey(chart, "axis.y.tick.0");
    const SceneNode* yTick1 = findByKey(chart, "axis.y.tick.1");
    const SceneNode* yTick2 = findByKey(chart, "axis.y.tick.2");
    ASSERT_NE(xTick0, nullptr);
    ASSERT_NE(yTick0, nullptr);
    ASSERT_NE(yTick1, nullptr);
    ASSERT_NE(yTick2, nullptr);

    // Vertical 方向下 "Spring" 按字符竖排为 "S\np\nr\ni\nn\ng"
    EXPECT_EQ(xTick0->text(), "S\np\nr\ni\nn\ng");
    EXPECT_DOUBLE_EQ(xTick0->style().fontSize, 13.0);
    EXPECT_EQ(xTick0->style().fill.color, Color::Hex(0x224488));
    EXPECT_NEAR(std::atan2(xTick0->transform().m10, xTick0->transform().m00), 0.35, 1e-6);

    // Y 轴刻度显示自定义 Text "Low" / "Mid" / "High"
    EXPECT_EQ(yTick0->text(), "Low");
    EXPECT_EQ(yTick1->text(), "Mid");
    EXPECT_EQ(yTick2->text(), "High");
    EXPECT_DOUBLE_EQ(yTick0->style().fontSize, 14.0);
    EXPECT_EQ(yTick0->style().fill.color, Color::Hex(0x884422));
    EXPECT_NEAR(std::atan2(yTick0->transform().m10, yTick0->transform().m00), -0.20, 1e-6);
}

TEST(LineChart, LinePointNodeMaintainsOwnHoverAndTooltip) {
    LineChart chart("line");
    chart.setFrame({10.0, 20.0, 600.0, 360.0});
    chart.setData(makeSampleDataset());

    LineChartConfig cfg;
    cfg.layout.x.tickLabels = {"Jan", "Feb", "Mar", "Apr"};
    cfg.layout.y.tickLabels = {"Zero", "Half", "Full"};
    cfg.pointStyle.radius = 4.0;
    cfg.pointStyle.hoverRadius = 7.0;
    cfg.pointStyle.haloRadius = 12.0;
    cfg.pointStyle.hitRadius = 10.0;
    chart.setConfig(cfg);

    // 初始帧：指针在画布外，4 个数据点均已生成但未 hover
    chart.tickTree(frameAt({-50.0, -50.0}));
    ASSERT_EQ(chart.pointNodes().size(), 4u);
    EXPECT_EQ(chart.hoveredPointNode(), nullptr);
    EXPECT_EQ(findHoveredTooltipTarget(chart), nullptr);

    LinePointNode* secondPoint = chart.pointNodes()[1];
    ASSERT_NE(secondPoint, nullptr);
    const SceneNode* halo = secondPoint->findChildByKey("halo");
    const SceneNode* dot = secondPoint->findChildByKey("dot");
    ASSERT_NE(halo, nullptr);
    ASSERT_NE(dot, nullptr);
    EXPECT_FALSE(halo->visible());
    EXPECT_DOUBLE_EQ(dot->frame().width, 8.0);  // 2 * radius(4.0)

    // 把指针移到第 2 个数据点（index = 1, value = 50 -> "Half"）的世界坐标中心附近（偏移 5px，在 hitRadius=10px 内）
    const Vec2d worldCenter = secondPoint->toWorld({0.0, 0.0});
    const Vec2d probe{worldCenter.x + 5.0, worldCenter.y - 3.0};
    chart.tickTree(frameAt(probe));

    // 子组件 LinePointNode 自己进入 hovered 状态，放大 dot 并显示 halo
    EXPECT_TRUE(secondPoint->hovered());
    EXPECT_EQ(chart.hoveredPointNode(), secondPoint);
    EXPECT_TRUE(halo->visible());
    EXPECT_DOUBLE_EQ(dot->frame().width, 14.0);  // 2 * hoverRadius(7.0)
    EXPECT_EQ(chart.cursorAt(probe), CursorShape::Pointer);

    // TooltipHost 通过 findHoveredTooltipTarget 直接命中该 LinePointNode
    TooltipTarget* target = findHoveredTooltipTarget(chart);
    ASSERT_EQ(target, secondPoint);
    EXPECT_TRUE(target->hasTooltipContent());

    const TooltipText tip = target->tooltipContent();
    EXPECT_EQ(tip.title, "Feb · Revenue");
    EXPECT_EQ(tip.body, "Half");

    const RectD anchor = target->tooltipAnchor();
    EXPECT_NEAR(anchor.center().x, worldCenter.x, 1e-6);
    EXPECT_NEAR(anchor.center().y, worldCenter.y, 1e-6);
    EXPECT_DOUBLE_EQ(anchor.width, 14.0);

    // 指针移开后，LinePointNode 自动恢复常态
    chart.tickTree(frameAt({-50.0, -50.0}));
    EXPECT_FALSE(secondPoint->hovered());
    EXPECT_FALSE(halo->visible());
    EXPECT_DOUBLE_EQ(dot->frame().width, 8.0);
    EXPECT_EQ(findHoveredTooltipTarget(chart), nullptr);
}
