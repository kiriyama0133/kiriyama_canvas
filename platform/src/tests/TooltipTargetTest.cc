#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "kiriyama/core/SceneQuery.h"
#include "kiriyama/math/Transform2D.h"
#include "kiriyama/scene/TooltipTarget.h"
#include "kiriyama/scene/Widget.h"

namespace {

using kiriyama::core::NodeType;
using kiriyama::core::RectD;
using kiriyama::core::SceneNode;
using kiriyama::core::Vec2d;
using kiriyama::scene::FrameContext;
using kiriyama::core::TooltipSide;
using kiriyama::scene::findHoveredTooltipTarget;
using kiriyama::scene::TooltipTarget;
using kiriyama::scene::TooltipText;
using kiriyama::scene::Widget;

[[nodiscard]] FrameContext at(double x, double y, bool down = false) {
    FrameContext ctx;
    ctx.time = 0.0;
    ctx.deltaTime = 0.016;
    ctx.pointer.position = Vec2d{x, y};
    ctx.pointer.down = down;
    return ctx;
}

[[nodiscard]] std::unique_ptr<TooltipTarget> makeTarget(std::string key, const RectD& frame) {
    auto target = std::make_unique<TooltipTarget>(std::move(key));
    target->setFrame(frame);
    return target;
}

// ---------------------------------------------------------------- 内容

TEST(TooltipTarget, StaticContentRoundTrip) {
    auto target = makeTarget("a", RectD{0.0, 0.0, 40.0, 40.0});
    target->setTooltipContent("CHG", "+1.24%");

    const TooltipText text = target->tooltipContent();
    EXPECT_EQ(text.title, "CHG");
    EXPECT_EQ(text.body, "+1.24%");
    EXPECT_TRUE(target->hasTooltipContent());
}

TEST(TooltipTarget, EmptyContentIsNotPickable) {
    auto target = makeTarget("a", RectD{0.0, 0.0, 40.0, 40.0});
    EXPECT_FALSE(target->hasTooltipContent());

    // 就算指针在上面，没有内容也不该被选中。
    Widget root("root");
    root.appendChild(std::move(target));
    root.tickTree(at(10.0, 10.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), nullptr);
}

/// 定制入口一：按当前数据生成内容（饼图扇区就是这种）。
class LiveTarget : public TooltipTarget {
public:
    explicit LiveTarget(std::string key) : TooltipTarget(std::move(key)) {}

    double value{0.0};
    double total{0.0};

public:
    TooltipText tooltipContent() const override {
        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "%.1f%%", 100.0 * value / total);
        return TooltipText{"LIVE", buffer};
    }
};

TEST(TooltipTarget, OverriddenContentUsesLiveData) {
    LiveTarget target("slice");
    target.setFrame(RectD{0.0, 0.0, 40.0, 40.0});
    target.value = 25.0;
    target.total = 200.0;

    const TooltipText text = target.tooltipContent();
    EXPECT_EQ(text.title, "LIVE");
    EXPECT_EQ(text.body, "12.5%");
}

TEST(TooltipTarget, DisabledTargetIsSkipped) {
    auto target = makeTarget("a", RectD{0.0, 0.0, 40.0, 40.0});
    target->setTooltipContent("t", "b");
    target->setTooltipEnabled(false);

    Widget root("root");
    root.appendChild(std::move(target));
    root.tickTree(at(10.0, 10.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), nullptr);
}

TEST(TooltipTarget, AnchorDefaultsToHitBounds) {
    auto target = makeTarget("a", RectD{10.0, 20.0, 40.0, 30.0});
    target->setTooltipContent("t", "b");

    const RectD anchor = target->tooltipAnchor();
    EXPECT_DOUBLE_EQ(anchor.x, 10.0);
    EXPECT_DOUBLE_EQ(anchor.y, 20.0);
    EXPECT_DOUBLE_EQ(anchor.width, 40.0);
    EXPECT_DOUBLE_EQ(anchor.height, 30.0);
}

TEST(TooltipTarget, SideIsConfigurable) {
    auto target = makeTarget("a", RectD{0.0, 0.0, 40.0, 40.0});
    EXPECT_EQ(target->tooltipSide(), TooltipSide::Above);
    target->setTooltipSide(TooltipSide::Right);
    EXPECT_EQ(target->tooltipSide(), TooltipSide::Right);
}

// ---------------------------------------------------------------- 挑选

TEST(TooltipTargetPick, HoveredTargetIsFound) {
    Widget root("root");
    auto target = makeTarget("a", RectD{0.0, 0.0, 40.0, 40.0});
    target->setTooltipContent("t", "b");
    TooltipTarget* raw = target.get();
    root.appendChild(std::move(target));

    root.tickTree(at(10.0, 10.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), raw);

    root.tickTree(at(200.0, 200.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), nullptr);
}

TEST(TooltipTargetPick, FocusBeatsHover) {
    Widget root("root");
    auto focused = makeTarget("focused", RectD{0.0, 0.0, 40.0, 40.0});
    focused->setTooltipContent("keyboard", "focus");
    TooltipTarget* raw = focused.get();
    root.appendChild(std::move(focused));

    auto hovered = makeTarget("hovered", RectD{100.0, 0.0, 40.0, 40.0});
    hovered->setTooltipContent("mouse", "hover");
    root.appendChild(std::move(hovered));

    // 指针在右边那个上，但左边那个拿了键盘焦点。
    raw->setTooltipFocused(true);
    root.tickTree(at(110.0, 10.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), raw);

    raw->setTooltipFocused(false);
    root.tickTree(at(110.0, 10.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), root.children()[1].get());
}

TEST(TooltipTargetPick, TopmostWinsWhenOverlapping) {
    Widget root("root");
    auto under = makeTarget("under", RectD{0.0, 0.0, 100.0, 100.0});
    under->setTooltipContent("under", "first");
    root.appendChild(std::move(under));
    auto over = makeTarget("over", RectD{0.0, 0.0, 100.0, 100.0});
    over->setTooltipContent("over", "second");
    TooltipTarget* raw = over.get();
    root.appendChild(std::move(over));

    root.tickTree(at(50.0, 50.0));
    // 后画的压在上面，气泡也该是它的。
    EXPECT_EQ(findHoveredTooltipTarget(root), raw);
}

TEST(TooltipTargetPick, SkipsInvisibleSubtree) {
    Widget root("root");
    auto group = std::make_unique<SceneNode>(NodeType::Group, "group");
    group->setVisible(false);
    auto target = makeTarget("hidden", RectD{0.0, 0.0, 40.0, 40.0});
    target->setTooltipContent("t", "b");
    group->appendChild(std::move(target));
    root.appendChild(std::move(group));

    root.tickTree(at(10.0, 10.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), nullptr);
}

TEST(TooltipTargetPick, DrillsThroughPlainGroups) {
    Widget root("root");
    auto group = std::make_unique<SceneNode>(NodeType::Group, "group");
    group->setTransform(kiriyama::math::Affine2D::Trs(Vec2d{100.0, 50.0}, 0.0, Vec2d{1.0, 1.0}));
    auto target = makeTarget("nested", RectD{0.0, 0.0, 40.0, 40.0});
    target->setTooltipContent("t", "b");
    TooltipTarget* raw = target.get();
    group->appendChild(std::move(target));
    root.appendChild(std::move(group));

    // 命中判定要走世界坐标：group 平移了 (100, 50)，所以本地 (10,10) 是世界 (110,60)。
    root.tickTree(at(110.0, 60.0));
    EXPECT_EQ(findHoveredTooltipTarget(root), raw);
}

}  // namespace
