#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "kiriyama/core/Interaction.h"

namespace {

using namespace kiriyama::core;
using kiriyama::math::Affine2D;

std::unique_ptr<SceneNode> makeRect(std::string key, RectD frame) {
    auto node = std::make_unique<SceneNode>(NodeType::Rect, std::move(key));
    node->setFrame(frame);
    SceneStyle style;
    style.fill = Color::FromBytes(0x3E, 0x6D, 0xB5);
    node->setStyle(style);
    return node;
}

/// root -> [a(0,0,100,100), b(200,0,100,100)]
std::unique_ptr<SceneNode> makeRoot() {
    auto root = std::make_unique<SceneNode>(NodeType::Group, "root");
    root->appendChild(makeRect("a", RectD{0.0, 0.0, 100.0, 100.0}));
    root->appendChild(makeRect("b", RectD{200.0, 0.0, 100.0, 100.0}));
    return root;
}

/// root -> g(Group) -> a(10,10,80,80)
std::unique_ptr<SceneNode> makeGroupedRoot() {
    auto root = std::make_unique<SceneNode>(NodeType::Group, "root");
    auto group = std::make_unique<SceneNode>(NodeType::Group, "g");
    group->appendChild(makeRect("a", RectD{10.0, 10.0, 80.0, 80.0}));
    root->appendChild(std::move(group));
    return root;
}

PointerEvent move(double x, double y) {
    return {PointerEventType::Move, {x, y}};
}

PointerEvent down(double x, double y) { return {PointerEventType::Down, {x, y}, 0}; }

PointerEvent up(double x, double y) { return {PointerEventType::Up, {x, y}, 0}; }

PointerEvent leave(double x, double y) { return {PointerEventType::Leave, {x, y}}; }

bool containsId(const std::vector<std::uint64_t>& ids, std::uint64_t id) {
    for (const std::uint64_t value : ids) {
        if (value == id) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- hover

TEST(Interaction, HoverFollowsPointer) {
    auto root = makeRoot();
    const std::uint64_t a = findByKey(*root, "a")->id();
    InteractionState state;

    const InteractionChange change = state.apply(*root, move(50.0, 50.0));
    EXPECT_TRUE(change.hoverChanged);
    EXPECT_TRUE(change.pointerInside);
    EXPECT_EQ(change.hoveredId, a);
    EXPECT_EQ(state.hoveredId(), a);
    EXPECT_TRUE(containsId(change.dirtyIds, a));
    EXPECT_TRUE(findByKey(*root, "a")->dirty());
}

TEST(Interaction, HoverSwitchesMarksBothDirty) {
    auto root = makeRoot();
    const std::uint64_t a = findByKey(*root, "a")->id();
    const std::uint64_t b = findByKey(*root, "b")->id();
    InteractionState state;

    state.apply(*root, move(50.0, 50.0));
    findByKey(*root, "a")->clearDirty();
    findByKey(*root, "b")->clearDirty();

    const InteractionChange change = state.apply(*root, move(250.0, 50.0));
    EXPECT_TRUE(change.hoverChanged);
    EXPECT_EQ(change.hoveredId, b);
    EXPECT_TRUE(containsId(change.dirtyIds, a));
    EXPECT_TRUE(containsId(change.dirtyIds, b));
    EXPECT_TRUE(findByKey(*root, "a")->dirty());
    EXPECT_TRUE(findByKey(*root, "b")->dirty());
}

TEST(Interaction, HoverClearsOnEmptyArea) {
    auto root = makeRoot();
    InteractionState state;
    state.apply(*root, move(50.0, 50.0));

    const InteractionChange change = state.apply(*root, move(500.0, 500.0));
    EXPECT_TRUE(change.hoverChanged);
    EXPECT_EQ(change.hoveredId, 0u);
    EXPECT_FALSE(state.hovered());
}

TEST(Interaction, LeaveClearsHoverAndInsideFlag) {
    auto root = makeRoot();
    InteractionState state;
    state.apply(*root, move(50.0, 50.0));

    const InteractionChange change = state.apply(*root, leave(50.0, 50.0));
    EXPECT_TRUE(change.hoverChanged);
    EXPECT_EQ(change.hoveredId, 0u);
    EXPECT_FALSE(change.pointerInside);
    EXPECT_FALSE(state.pointerInside());
}

TEST(Interaction, HoverChainIsRootToAnchor) {
    auto root = makeRoot();
    const std::uint64_t rootId = root->id();
    const std::uint64_t a = findByKey(*root, "a")->id();
    InteractionState state;
    state.apply(*root, move(50.0, 50.0));

    const auto chain = state.hoverChain();
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_EQ(chain[0], rootId);
    EXPECT_EQ(chain[1], a);
}

// ---------------------------------------------------------------- active / click

TEST(Interaction, PressSetsActiveAndClickOnSameNode) {
    auto root = makeRoot();
    const std::uint64_t a = findByKey(*root, "a")->id();
    InteractionState state;

    const InteractionChange press = state.apply(*root, down(50.0, 50.0));
    EXPECT_TRUE(press.activeChanged);
    EXPECT_EQ(press.activeId, a);
    EXPECT_TRUE(state.pressed());
    EXPECT_EQ(state.pressedButton(), 0);

    const InteractionChange release = state.apply(*root, up(50.0, 50.0));
    EXPECT_TRUE(release.activeChanged);
    EXPECT_EQ(release.releasedId, a);
    EXPECT_EQ(release.clickedId, a);
    EXPECT_TRUE(release.clicked());
    EXPECT_EQ(state.activeId(), 0u);
}

TEST(Interaction, ReleaseElsewhereDoesNotClick) {
    auto root = makeRoot();
    const std::uint64_t a = findByKey(*root, "a")->id();
    InteractionState state;

    state.apply(*root, down(50.0, 50.0));
    const InteractionChange release = state.apply(*root, up(250.0, 50.0));
    EXPECT_EQ(release.releasedId, a);
    EXPECT_EQ(release.clickedId, 0u);
    EXPECT_EQ(release.hoveredId, findByKey(*root, "b")->id());
}

TEST(Interaction, HoverFrozenWhilePressed) {
    auto root = makeRoot();
    const std::uint64_t a = findByKey(*root, "a")->id();
    InteractionState state;

    state.apply(*root, down(50.0, 50.0));
    const InteractionChange change = state.apply(*root, move(250.0, 50.0));
    EXPECT_FALSE(change.hoverChanged);
    EXPECT_EQ(state.hoveredId(), a);
}

TEST(Interaction, CancelReleasesWithoutClick) {
    auto root = makeRoot();
    const std::uint64_t a = findByKey(*root, "a")->id();
    InteractionState state;

    state.apply(*root, down(50.0, 50.0));
    const InteractionChange cancel =
        state.apply(*root, {PointerEventType::Cancel, {50.0, 50.0}, 0});
    EXPECT_EQ(cancel.releasedId, a);
    EXPECT_EQ(cancel.clickedId, 0u);
    EXPECT_FALSE(state.pressed());
}

TEST(Interaction, ActiveStyleWinsOverHover) {
    auto root = makeRoot();
    InteractionOptions options;
    options.hover.overlayFill = Color::FromBytes(0xFF, 0xFF, 0xFF, 0x40);
    options.active.overlayFill = Color::FromBytes(0x00, 0x00, 0x00, 0x60);
    InteractionState state(options);

    state.apply(*root, move(50.0, 50.0));
    const SceneNode& a = *findByKey(*root, "a");
    EXPECT_EQ(state.styleFor(a), options.hover);

    state.apply(*root, down(50.0, 50.0));
    EXPECT_EQ(state.styleFor(a), options.active);

    state.apply(*root, up(50.0, 50.0));
    EXPECT_EQ(state.styleFor(a), options.hover);
}

// ---------------------------------------------------------------- 锚点上提

TEST(Interaction, AnchorLevelPicksAncestor) {
    auto root = makeGroupedRoot();
    const std::uint64_t group = findByKey(*root, "g")->id();
    InteractionOptions options;
    options.anchorLevel = 1;
    InteractionState state(options);

    const InteractionChange change = state.apply(*root, move(50.0, 50.0));
    EXPECT_EQ(change.hoveredId, group);
}

TEST(Interaction, GroupHoverInheritedByDescendants) {
    auto root = makeGroupedRoot();
    InteractionOptions options;
    options.anchorLevel = 1;
    options.hover.overlayFill = Color::FromBytes(0x00, 0x00, 0x00, 0x30);
    InteractionState state(options);
    state.apply(*root, move(50.0, 50.0));

    const SceneNode& group = *findByKey(*root, "g");
    const SceneNode& leaf = *findByKey(*root, "a");
    EXPECT_EQ(state.styleFor(group), options.hover);
    EXPECT_EQ(state.styleFor(leaf), options.hover);
    EXPECT_TRUE(state.isHovered(leaf));
}

TEST(Interaction, PointsCursorFlagFromHoverStyle) {
    auto root = makeRoot();
    InteractionOptions options;
    options.hover.pointerCursor = true;
    InteractionState state(options);

    EXPECT_FALSE(state.wantsPointerCursor());
    state.apply(*root, move(50.0, 50.0));
    EXPECT_TRUE(state.wantsPointerCursor());
    state.apply(*root, move(500.0, 500.0));
    EXPECT_FALSE(state.wantsPointerCursor());
}

// ---------------------------------------------------------------- 坐标 / 失效

TEST(Interaction, ViewTransformMapsCanvasToWorld) {
    auto root = makeRoot();
    const std::uint64_t a = findByKey(*root, "a")->id();
    InteractionState state;
    state.setViewTransform(Affine2D::Translation({100.0, 0.0}));

    const InteractionChange change = state.apply(*root, move(150.0, 50.0));
    EXPECT_EQ(change.hoveredId, a);
    EXPECT_NEAR(state.pointer().x, 50.0, 1e-9);
}

TEST(Interaction, StaleHoverPrunedAfterNodeRemoved) {
    auto root = makeRoot();
    InteractionState state;
    state.apply(*root, move(50.0, 50.0));
    ASSERT_TRUE(state.hovered());

    root->removeChild(findByKey(*root, "a"));
    const InteractionChange change = state.apply(*root, move(50.0, 50.0));
    EXPECT_TRUE(change.hoverChanged);
    EXPECT_EQ(change.hoveredId, 0u);
    EXPECT_TRUE(state.hoverChain().empty());
}

// ---------------------------------------------------------------- 样式合成

TEST(InteractionStyle, BlendsOverlayOntoFill) {
    SceneStyle base;
    base.fill = Color::Rgba(1.0, 0.0, 0.0, 1.0);
    InteractionStyle overlay;
    overlay.overlayFill = Color::Rgba(0.0, 0.0, 1.0, 0.5);

    const SceneStyle result = applyInteractionStyle(base, overlay);
    EXPECT_NEAR(result.fill.r, 0.5, 1e-6);
    EXPECT_NEAR(result.fill.g, 0.0, 1e-6);
    EXPECT_NEAR(result.fill.b, 0.5, 1e-6);
    EXPECT_NEAR(result.fill.a, 1.0, 1e-6);
}

TEST(InteractionStyle, OverlayFillOnTransparentNodeStaysVisible) {
    SceneStyle base;  // fill.a == 0
    InteractionStyle overlay;
    overlay.overlayFill = Color::Rgba(0.0, 1.0, 0.0, 0.4);

    const SceneStyle result = applyInteractionStyle(base, overlay);
    EXPECT_NEAR(result.fill.g, 1.0, 1e-6);
    EXPECT_NEAR(result.fill.a, 0.4, 1e-6);
}

TEST(InteractionStyle, AddsStrokeWidthAndMultipliesOpacity) {
    SceneStyle base;
    base.strokeWidth = 2.0;
    InteractionStyle overlay;
    overlay.overlayStroke = Color::Rgba(0.0, 0.0, 0.0, 1.0);
    overlay.strokeWidthDelta = 1.5;
    overlay.opacityScale = 0.5;

    const SceneStyle result = applyInteractionStyle(base, overlay);
    EXPECT_NEAR(result.strokeWidth, 3.5, 1e-9);
    EXPECT_NEAR(result.opacity, 0.5, 1e-9);
}

TEST(InteractionStyle, EmptyOverlayIsNoOp) {
    SceneStyle base;
    base.fill = Color::Rgba(0.2, 0.4, 0.6, 1.0);
    base.strokeWidth = 1.0;
    base.opacity = 0.8;

    EXPECT_EQ(applyInteractionStyle(base, InteractionStyle{}), base);
}

TEST(InteractionStyle, TransformScalesAboutBoundsCenter) {
    auto root = makeRoot();
    const SceneNode& a = *findByKey(*root, "a");
    InteractionStyle overlay;
    overlay.scale = 2.0;

    const Affine2D matrix = interactionTransform(a, overlay);
    const auto topLeft = matrix.apply({0.0, 0.0});
    const auto center = matrix.apply({50.0, 50.0});
    EXPECT_NEAR(topLeft.x, -50.0, 1e-9);
    EXPECT_NEAR(topLeft.y, -50.0, 1e-9);
    EXPECT_NEAR(center.x, 50.0, 1e-9);
    EXPECT_NEAR(center.y, 50.0, 1e-9);
}

TEST(InteractionStyle, UnitScaleReturnsIdentity) {
    auto root = makeRoot();
    const SceneNode& a = *findByKey(*root, "a");
    EXPECT_TRUE(interactionTransform(a, InteractionStyle{}).isIdentity());
}

}  // namespace
