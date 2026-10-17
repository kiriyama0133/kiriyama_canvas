#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "kiriyama/core/Chart.h"

namespace {

using kiriyama::core::Affine2D;
using kiriyama::core::collectDirtyNodes;
using kiriyama::core::countNodes;
using kiriyama::core::CursorShape;
using kiriyama::core::depthOf;
using kiriyama::core::findById;
using kiriyama::core::findByKey;
using kiriyama::core::findFirst;
using kiriyama::core::hitTestScene;
using kiriyama::core::NodeType;
using kiriyama::core::RectD;
using kiriyama::core::resolveCursor;
using kiriyama::core::SceneNode;
using kiriyama::core::SceneStyle;
using kiriyama::math::Vec2d;

constexpr double kTol = 1e-9;

SceneStyle filledStyle() {
    SceneStyle style;
    style.fill = kiriyama::core::Color::FromBytes(64, 128, 255);
    return style;
}

std::unique_ptr<SceneNode> makeRect(const std::string& key, const RectD& frame) {
    auto node = std::make_unique<SceneNode>(NodeType::Rect, key);
    node->setFrame(frame);
    node->setStyle(filledStyle());
    return node;
}

// ---------------------------------------------------------------- 变换

TEST(SceneTransform, MultiplicationOrder) {
    const Affine2D translate = Affine2D::Translation({10.0, 0.0});
    const Affine2D scale = Affine2D::Scaling({2.0, 2.0});

    // a * b 表示"先 b 后 a"
    const Vec2d scaledThenMoved = (translate * scale).apply({1.0, 1.0});
    EXPECT_EQ(scaledThenMoved, (Vec2d{12.0, 2.0}));

    const Vec2d movedThenScaled = (scale * translate).apply({1.0, 1.0});
    EXPECT_EQ(movedThenScaled, (Vec2d{22.0, 2.0}));
}

TEST(SceneTransform, TrsComposesScaleThenRotationThenTranslation) {
    const Affine2D matrix = Affine2D::Trs({10.0, 20.0}, kiriyama::math::kHalfPi, {2.0, 2.0});
    const Vec2d result = matrix.apply({1.0, 0.0});
    EXPECT_NEAR(result.x, 10.0, kTol);
    EXPECT_NEAR(result.y, 22.0, kTol);
}

TEST(SceneTransform, InverseRoundTrip) {
    const Affine2D matrix = Affine2D::Trs({30.0, -5.0}, 0.7, {2.0, 0.5});
    const Vec2d point{3.0, 4.0};

    const Vec2d back = matrix.inverse().apply(matrix.apply(point));
    EXPECT_NEAR(back.x, point.x, 1e-9);
    EXPECT_NEAR(back.y, point.y, 1e-9);
}

TEST(SceneTransform, ApplyVectorIgnoresTranslation) {
    const Affine2D matrix = Affine2D::Translation({100.0, 100.0}) * Affine2D::Scaling({3.0, 3.0});
    EXPECT_EQ(matrix.applyVector({1.0, 0.0}), (Vec2d{3.0, 0.0}));
}

TEST(SceneTransform, DegenerateInverseFallsBackToIdentity) {
    const Affine2D degenerate = Affine2D::Scaling({0.0, 1.0});
    EXPECT_FALSE(degenerate.invertible());
    EXPECT_EQ(degenerate.inverse(), Affine2D::Identity());

    // 不能因此产生 NaN
    const Vec2d result = degenerate.inverse().apply({5.0, 7.0});
    EXPECT_TRUE(std::isfinite(result.x));
    EXPECT_TRUE(std::isfinite(result.y));
}

// ---------------------------------------------------------------- 节点基础

TEST(SceneNode, IdentityAndDefaults) {
    const SceneNode a(NodeType::Rect, "bar");
    const SceneNode b(NodeType::Rect, "bar");

    EXPECT_NE(a.id(), b.id());
    EXPECT_EQ(static_cast<int>(a.type()), static_cast<int>(NodeType::Rect));
    EXPECT_EQ(a.key(), "bar");
    EXPECT_TRUE(a.visible());
    EXPECT_TRUE(a.dirty());
    EXPECT_EQ(a.parent(), nullptr);
    EXPECT_EQ(a.childCount(), 0u);

    EXPECT_STREQ(kiriyama::core::toString(NodeType::Rect), "rect");
    EXPECT_STREQ(kiriyama::core::toString(NodeType::Polygon), "polygon");
}

TEST(SceneNode, ColorFactories) {
    const auto rgb = kiriyama::core::Color::Rgb(1.0, 0.5, 0.25);
    EXPECT_FLOAT_EQ(rgb.a, 1.0f);
    EXPECT_FLOAT_EQ(rgb.g, 0.5f);

    const auto bytes = kiriyama::core::Color::FromBytes(255, 128, 0);
    EXPECT_NEAR(bytes.r, 1.0, 1e-6);
    EXPECT_NEAR(bytes.g, 128.0 / 255.0, 1e-6);
    EXPECT_FLOAT_EQ(bytes.a, 1.0f);

    // 默认全透明 = 不可见
    const kiriyama::core::Color empty;
    EXPECT_FALSE(empty.visible());
}

TEST(SceneNode, TreeStructure) {
    SceneNode root;
    SceneNode* a = root.appendChild(std::make_unique<SceneNode>(NodeType::Rect, "a"));
    SceneNode* b = root.appendChild(std::make_unique<SceneNode>(NodeType::Rect, "b"));
    SceneNode* c = root.insertChild(1, std::make_unique<SceneNode>(NodeType::Rect, "c"));

    ASSERT_EQ(root.childCount(), 3u);
    EXPECT_EQ(root.childAt(0), a);
    EXPECT_EQ(root.childAt(1), c);
    EXPECT_EQ(root.childAt(2), b);
    EXPECT_EQ(c->parent(), &root);
    EXPECT_EQ(c->indexInParent(), 1);
    EXPECT_EQ(root.indexInParent(), -1);

    EXPECT_EQ(root.childAt(99), nullptr);
    EXPECT_EQ(root.insertChild(99, std::make_unique<SceneNode>()) != nullptr, true);

    auto removed = root.removeChild(b);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->parent(), nullptr);
    EXPECT_EQ(root.childCount(), 3u);  // 99 被夹到末尾后追加了一个

    EXPECT_EQ(root.removeChild(static_cast<std::size_t>(99)), nullptr);
    EXPECT_EQ(root.removeChild(static_cast<SceneNode*>(nullptr)), nullptr);
}

TEST(SceneNode, TakeAndRestoreChildren) {
    SceneNode root;
    root.appendChild(std::make_unique<SceneNode>(NodeType::Rect, "a"));
    root.appendChild(std::make_unique<SceneNode>(NodeType::Rect, "b"));

    auto taken = root.takeChildren();
    EXPECT_EQ(taken.size(), 2u);
    EXPECT_EQ(root.childCount(), 0u);
    // 父指针不因搬走而失效，接回来即可
    EXPECT_EQ(taken[0]->parent(), &root);

    root.setChildren(std::move(taken));
    EXPECT_EQ(root.childCount(), 2u);
    EXPECT_EQ(root.childAt(0)->parent(), &root);
}

TEST(SceneNode, SetChildrenWithoutStructuralChangeDoesNotDirty) {
    SceneNode root;
    root.appendChild(std::make_unique<SceneNode>(NodeType::Rect, "a"));
    root.clearSubtreeDirty();

    auto taken = root.takeChildren();
    root.setChildren(std::move(taken), false);
    EXPECT_FALSE(root.dirty());
}

TEST(SceneNode, CloneIsDeepAndFreshIdentity) {
    auto root = std::make_unique<SceneNode>(NodeType::Group, "root");
    auto child = makeRect("child", RectD{0.0, 0.0, 10.0, 10.0});
    child->setName("标签");
    SceneNode* original = root->appendChild(std::move(child));

    auto copy = root->clone();
    ASSERT_EQ(copy->childCount(), 1u);
    EXPECT_NE(copy->id(), root->id());
    EXPECT_EQ(copy->key(), "root");
    EXPECT_EQ(copy->childAt(0)->key(), "child");
    EXPECT_EQ(copy->childAt(0)->name(), "标签");
    EXPECT_NE(copy->childAt(0)->id(), original->id());
    EXPECT_EQ(copy->childAt(0)->frame(), (RectD{0.0, 0.0, 10.0, 10.0}));
    EXPECT_EQ(copy->childAt(0)->parent(), copy.get());

    // 深拷贝：改副本不影响原件
    copy->childAt(0)->setFrame(RectD{0.0, 0.0, 99.0, 99.0});
    EXPECT_EQ(original->frame(), (RectD{0.0, 0.0, 10.0, 10.0}));
}

// ---------------------------------------------------------------- 几何

TEST(SceneNode, LocalBoundsFollowNodeType) {
    SceneNode rect(NodeType::Rect);
    rect.setFrame(RectD{10.0, 20.0, 30.0, 40.0});
    EXPECT_EQ(rect.localBounds(), (RectD{10.0, 20.0, 30.0, 40.0}));

    SceneNode polyline(NodeType::Polyline);
    polyline.setPoints({{0.0, 5.0}, {10.0, -5.0}, {20.0, 0.0}});
    EXPECT_EQ(polyline.localBounds(), (RectD::FromLTRB(0.0, -5.0, 20.0, 5.0)));

    SceneNode group(NodeType::Group);
    EXPECT_TRUE(group.localBounds().empty());
}

TEST(SceneNode, BoundsIncludeTransformedChildren) {
    SceneNode root;
    auto child = makeRect("c", RectD{0.0, 0.0, 20.0, 20.0});
    child->setTranslation({100.0, 50.0});
    root.appendChild(std::move(child));

    EXPECT_EQ(root.bounds(), (RectD::FromLTRB(100.0, 50.0, 120.0, 70.0)));

    // 不可见的子节点不撑大包围盒
    root.childAt(0)->setVisible(false);
    EXPECT_TRUE(root.bounds().empty());
}

TEST(SceneNode, WorldTransformWalksParentChain) {
    SceneNode root;
    root.setTranslation({10.0, 20.0});

    auto child = std::make_unique<SceneNode>(NodeType::Group, "child");
    child->setTranslation({5.0, 5.0});
    SceneNode* childRaw = root.appendChild(std::move(child));

    EXPECT_EQ(childRaw->toWorld({0.0, 0.0}), (Vec2d{15.0, 25.0}));

    auto leaf = makeRect("leaf", RectD{0.0, 0.0, 4.0, 4.0});
    leaf->setTranslation({1.0, 1.0});
    SceneNode* leafRaw = childRaw->appendChild(std::move(leaf));
    EXPECT_EQ(leafRaw->toWorld({0.0, 0.0}), (Vec2d{16.0, 26.0}));
}

TEST(SceneNode, RotationIsInheritedByChildren) {
    SceneNode root;
    root.setTransform(Affine2D::Rotation(kiriyama::math::kHalfPi));

    auto child = std::make_unique<SceneNode>(NodeType::Group);
    child->setTranslation({10.0, 0.0});
    SceneNode* childRaw = root.appendChild(std::move(child));

    // 父旋转 90° 后，子节点原点跑到 (0, 10)
    const Vec2d world = childRaw->toWorld({0.0, 0.0});
    EXPECT_NEAR(world.x, 0.0, 1e-9);
    EXPECT_NEAR(world.y, 10.0, 1e-9);
}

TEST(SceneNode, ToLocalIsInverseOfToWorld) {
    SceneNode root;
    root.setTransform(Affine2D::Trs({40.0, 12.0}, 0.4, {1.5, 0.8}));

    auto child = makeRect("c", RectD{0.0, 0.0, 10.0, 10.0});
    child->setTranslation({7.0, 3.0});
    SceneNode* childRaw = root.appendChild(std::move(child));

    const Vec2d localPoint{2.5, 6.5};
    const Vec2d back = childRaw->toLocal(childRaw->toWorld(localPoint));
    EXPECT_NEAR(back.x, localPoint.x, 1e-9);
    EXPECT_NEAR(back.y, localPoint.y, 1e-9);
}

TEST(SceneNode, WorldBoundsIncludeRotation) {
    SceneNode root;
    root.setTransform(Affine2D::Rotation(kiriyama::math::kHalfPi));

    auto child = makeRect("c", RectD{0.0, 0.0, 20.0, 10.0});
    root.appendChild(std::move(child));

    // 旋转 90° 后 20x10 变成 10x20（包围盒轴对齐）
    const RectD world = root.worldBounds();
    EXPECT_NEAR(world.width, 10.0, 1e-9);
    EXPECT_NEAR(world.height, 20.0, 1e-9);
    EXPECT_NEAR(world.left(), -10.0, 1e-9);
    EXPECT_NEAR(world.top(), 0.0, 1e-9);
}

TEST(SceneNode, HitTestLocalForRectFillAndStroke) {
    SceneNode node(NodeType::Rect);
    node.setFrame(RectD{0.0, 0.0, 100.0, 50.0});

    // 无样式：既没填充也没描边
    EXPECT_FALSE(node.containsLocal({50.0, 25.0}));

    SceneStyle style;
    style.fill = kiriyama::core::Color::Rgb(1.0, 0.0, 0.0);
    node.setStyle(style);
    EXPECT_TRUE(node.containsLocal({50.0, 25.0}));
    EXPECT_FALSE(node.containsLocal({150.0, 25.0}));

    // 只有描边时，只有边框附近能命中
    SceneStyle strokeOnly;
    strokeOnly.stroke = kiriyama::core::Color::Rgb(0.0, 0.0, 0.0);
    strokeOnly.strokeWidth = 4.0;
    node.setStyle(strokeOnly);
    EXPECT_FALSE(node.containsLocal({50.0, 25.0}));  // 内部不命中
    EXPECT_TRUE(node.containsLocal({1.0, 25.0}));    // 左边框
    EXPECT_TRUE(node.containsLocal({50.0, 2.0}));    // 距上边框 2 = 半线宽
    EXPECT_FALSE(node.containsLocal({50.0, 2.5}));   // 差一点点就点不中
}

TEST(SceneNode, HitTestLocalForEllipse) {
    SceneNode node(NodeType::Ellipse);
    node.setFrame(RectD{0.0, 0.0, 100.0, 100.0});
    node.setStyle(filledStyle());

    EXPECT_TRUE(node.containsLocal({50.0, 50.0}));
    EXPECT_TRUE(node.containsLocal({50.0, 1.0}));
    // 角落不在椭圆内
    EXPECT_FALSE(node.containsLocal({1.0, 1.0}));
}

TEST(SceneNode, HitTestLocalForLineUsesTolerance) {
    SceneNode node(NodeType::Line);
    node.setPoints({{0.0, 0.0}, {100.0, 0.0}});

    SceneStyle style;
    style.stroke = kiriyama::core::Color::Rgb(0.0, 0.0, 0.0);
    style.strokeWidth = 2.0;
    node.setStyle(style);

    EXPECT_TRUE(node.containsLocal({50.0, 1.0}));
    EXPECT_FALSE(node.containsLocal({50.0, 5.0}));
    // 放宽容差后能点中细线
    EXPECT_TRUE(node.containsLocal({50.0, 5.0}, 4.0));
    EXPECT_FALSE(node.containsLocal({50.0, 50.0}, 4.0));
}

TEST(SceneNode, HitTestLocalForPolygon) {
    SceneNode node(NodeType::Polygon);
    node.setPoints({{0.0, 0.0}, {100.0, 0.0}, {100.0, 100.0}, {0.0, 100.0}});
    node.setStyle(filledStyle());

    EXPECT_TRUE(node.containsLocal({50.0, 50.0}));
    EXPECT_FALSE(node.containsLocal({150.0, 50.0}));
}

TEST(SceneNode, HitTestLocalForTextUsesFrame) {
    SceneNode node(NodeType::Text);
    node.setText("hello");
    node.setFrame(RectD{0.0, 0.0, 40.0, 16.0});

    EXPECT_TRUE(node.containsLocal({20.0, 8.0}));
    EXPECT_FALSE(node.containsLocal({60.0, 8.0}));
    EXPECT_TRUE(node.containsLocal({45.0, 8.0}, 8.0));
}

// ---------------------------------------------------------------- 命中

TEST(SceneHit, TopmostNodeWins) {
    SceneNode root;
    auto bottom = makeRect("bottom", RectD{0.0, 0.0, 100.0, 100.0});
    auto top = makeRect("top", RectD{20.0, 20.0, 30.0, 30.0});
    root.appendChild(std::move(bottom));
    root.appendChild(std::move(top));

    const auto hit = hitTestScene(root, {30.0, 30.0});
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit.node->key(), "top");

    // 只有底下的矩形覆盖这里
    const auto hit2 = hitTestScene(root, {5.0, 5.0});
    ASSERT_TRUE(hit2);
    EXPECT_EQ(hit2.node->key(), "bottom");
}

TEST(SceneHit, ChainRunsFromRootToNode) {
    SceneNode root;
    auto group = std::make_unique<SceneNode>(NodeType::Group, "g");
    SceneNode* groupRaw = root.appendChild(std::move(group));
    groupRaw->setTranslation({10.0, 10.0});
    auto rect = makeRect("r", RectD{0.0, 0.0, 50.0, 50.0});
    SceneNode* rectRaw = groupRaw->appendChild(std::move(rect));

    const auto hit = hitTestScene(root, {20.0, 20.0});
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit.node, rectRaw);
    ASSERT_EQ(hit.chain.size(), 3u);
    EXPECT_EQ(hit.chain[0], &root);
    EXPECT_EQ(hit.chain[1], groupRaw);
    EXPECT_EQ(hit.chain[2], rectRaw);

    // 命中点的局部坐标
    EXPECT_EQ(hit.localPoint, (Vec2d{10.0, 10.0}));
    EXPECT_EQ(hit.worldTransform.apply({0.0, 0.0}), (Vec2d{10.0, 10.0}));
}

TEST(SceneHit, InvisibleSubtreeIsSkipped) {
    SceneNode root;
    auto hidden = makeRect("hidden", RectD{0.0, 0.0, 100.0, 100.0});
    hidden->setVisible(false);
    root.appendChild(std::move(hidden));

    EXPECT_FALSE(hitTestScene(root, {50.0, 50.0}));
}

TEST(SceneHit, GroupIsNeverReturned) {
    SceneNode root;
    auto group = std::make_unique<SceneNode>(NodeType::Group);
    group->setFrame(RectD{0.0, 0.0, 100.0, 100.0});
    root.appendChild(std::move(group));

    // 组只是容器，空白区域不算命中
    EXPECT_FALSE(hitTestScene(root, {50.0, 50.0}));
}

TEST(SceneHit, ZeroThicknessNodeIsStillHittable) {
    SceneNode root;
    auto line = std::make_unique<SceneNode>(NodeType::Line, "grid");
    line->setPoints({{0.0, 0.0}, {100.0, 0.0}});
    SceneStyle style;
    style.stroke = kiriyama::core::Color::Rgb(0.0, 0.0, 0.0);
    style.strokeWidth = 1.0;
    line->setStyle(style);
    root.appendChild(std::move(line));

    // 包围盒高度为 0，必须靠 tolerance 才能命中
    EXPECT_FALSE(hitTestScene(root, {50.0, 6.0}, 0.0));
    const auto hit = hitTestScene(root, {50.0, 3.0}, 4.0);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit.node->key(), "grid");
}

TEST(SceneHit, MissReturnsEmptyResult) {
    SceneNode root;
    root.appendChild(makeRect("r", RectD{0.0, 0.0, 10.0, 10.0}));

    const auto hit = hitTestScene(root, {500.0, 500.0});
    EXPECT_FALSE(hit);
    EXPECT_EQ(hit.node, nullptr);
    EXPECT_TRUE(hit.chain.empty());
}

TEST(SceneHit, RespectsRotation) {
    SceneNode root;
    root.setTransform(Affine2D::Translation({100.0, 0.0}) *
                      Affine2D::Rotation(kiriyama::math::kHalfPi));

    auto rect = makeRect("r", RectD{0.0, 0.0, 40.0, 10.0});
    root.appendChild(std::move(rect));

    // 旋转 90° 后，40x10 的横条变成竖条：x ∈ [90, 100]，y ∈ [0, 40]
    const auto hit = hitTestScene(root, {95.0, 20.0});
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit.node->key(), "r");

    // 横条原来的位置（y 方向 20 以内、x 过 100）已经不在图形上
    EXPECT_FALSE(hitTestScene(root, {105.0, 20.0}));
    EXPECT_FALSE(hitTestScene(root, {95.0, 50.0}));
}

// ---------------------------------------------------------------- 查询

namespace {

std::unique_ptr<SceneNode> makeQueryTree() {
    auto root = std::make_unique<SceneNode>(NodeType::Group, "root");
    auto a = std::make_unique<SceneNode>(NodeType::Group, "a");
    auto a1 = std::make_unique<SceneNode>(NodeType::Rect, "a1");
    auto a2 = std::make_unique<SceneNode>(NodeType::Text, "a2");
    a->appendChild(std::move(a1));
    a->appendChild(std::move(a2));
    root->appendChild(std::move(a));
    root->appendChild(std::make_unique<SceneNode>(NodeType::Rect, "b"));
    return root;
}

}  // namespace

TEST(SceneQuery, DepthFirstOrder) {
    auto root = makeQueryTree();
    std::vector<std::string> keys;
    kiriyama::core::forEachDepthFirst(*root, [&](const SceneNode& node) {
        keys.push_back(node.key());
    });
    EXPECT_EQ(keys, (std::vector<std::string>{"root", "a", "a1", "a2", "b"}));
}

TEST(SceneQuery, BreadthFirstOrder) {
    auto root = makeQueryTree();
    std::vector<std::string> keys;
    kiriyama::core::forEachBreadthFirst(*root, [&](const SceneNode& node) {
        keys.push_back(node.key());
    });
    EXPECT_EQ(keys, (std::vector<std::string>{"root", "a", "b", "a1", "a2"}));
}

TEST(SceneQuery, FindByKeyAndId) {
    auto root = makeQueryTree();
    SceneNode* a2 = findByKey(*root, "a2");
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(a2->type(), NodeType::Text);

    EXPECT_EQ(findByKey(*root, "nope"), nullptr);
    EXPECT_EQ(findByKey(*root, ""), nullptr);

    EXPECT_EQ(findById(*root, a2->id()), a2);
    EXPECT_EQ(findById(*root, 999999u), nullptr);
}

TEST(SceneQuery, CollectAndCount) {
    auto root = makeQueryTree();
    EXPECT_EQ(countNodes(*root), 5u);

    const auto rects = kiriyama::core::collectNodes(*root, [](const SceneNode& node) {
        return node.type() == NodeType::Rect;
    });
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_EQ(rects[0]->key(), "a1");
    EXPECT_EQ(rects[1]->key(), "b");

    const SceneNode* firstGroup = findFirst(*root, [](const SceneNode& node) {
        return node.type() == NodeType::Group && node.key() != "root";
    });
    ASSERT_NE(firstGroup, nullptr);
    EXPECT_EQ(firstGroup->key(), "a");
}

TEST(SceneQuery, DepthAndAncestors) {
    auto root = makeQueryTree();
    SceneNode* a2 = findByKey(*root, "a2");
    ASSERT_NE(a2, nullptr);

    EXPECT_EQ(depthOf(*root), 0);
    EXPECT_EQ(depthOf(*a2), 2);

    const auto chain = kiriyama::core::ancestorChain(*a2);
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(chain[0], a2);
    EXPECT_EQ(chain[2], root.get());
}

TEST(SceneQuery, DirtyCollection) {
    auto root = makeQueryTree();
    root->clearSubtreeDirty();
    EXPECT_FALSE(root->hasDirtyInSubtree());
    EXPECT_TRUE(collectDirtyNodes(*root).empty());

    SceneNode* a1 = findByKey(*root, "a1");
    a1->setFrame(RectD{0.0, 0.0, 5.0, 5.0});

    EXPECT_TRUE(root->hasDirtyInSubtree());
    const auto dirty = collectDirtyNodes(*root);
    ASSERT_EQ(dirty.size(), 1u);
    EXPECT_EQ(dirty[0], a1);

    // 只清子树不清父节点，父节点依然"有脏后代"
    a1->clearDirty();
    root->markDirty();
    EXPECT_TRUE(root->hasDirtyInSubtree());
    root->clearSubtreeDirty();
    EXPECT_FALSE(root->hasDirtyInSubtree());
}

TEST(SceneQuery, MutatorsMarkDirty) {
    SceneNode node(NodeType::Rect);
    node.clearDirty();

    node.setFrame(RectD{0.0, 0.0, 1.0, 1.0});
    EXPECT_TRUE(node.dirty());

    node.clearDirty();
    node.setFrame(RectD{0.0, 0.0, 1.0, 1.0});  // 同值不应标脏
    EXPECT_FALSE(node.dirty());

    node.setVisible(false);
    EXPECT_TRUE(node.dirty());
}

// ---------------------------------------------------------------- 光标继承

TEST(SceneCursor, RootWithNoDeclarationIsDefault) {
    SceneNode root(NodeType::Group, "root");
    EXPECT_EQ(resolveCursor(root), CursorShape::Default);
}

TEST(SceneCursor, NearestDeclarationWins) {
    SceneNode root(NodeType::Group, "root");
    root.mutableStyle().cursor = CursorShape::Pointer;
    auto child = std::make_unique<SceneNode>(NodeType::Rect, "child");
    SceneNode& childRef = *child;
    root.appendChild(std::move(child));

    EXPECT_EQ(resolveCursor(root), CursorShape::Pointer);
    // 子节点没声明，继承父节点的手型。
    EXPECT_EQ(resolveCursor(childRef), CursorShape::Pointer);
}

TEST(SceneCursor, ChildOverridesAncestor) {
    SceneNode root(NodeType::Group, "root");
    root.mutableStyle().cursor = CursorShape::Pointer;
    auto child = std::make_unique<SceneNode>(NodeType::Rect, "child");
    child->mutableStyle().cursor = CursorShape::ResizeH;
    SceneNode& childRef = *child;
    auto grandChild = std::make_unique<SceneNode>(NodeType::Text, "label");
    // 文本节点没声明，继承最近的显式声明（ResizeH），不是更远的 Pointer。
    SceneNode& grandRef = *grandChild;
    child->appendChild(std::move(grandChild));
    root.appendChild(std::move(child));

    EXPECT_EQ(resolveCursor(childRef), CursorShape::ResizeH);
    EXPECT_EQ(resolveCursor(grandRef), CursorShape::ResizeH);
}

TEST(SceneCursor, ExplicitDefaultStopsInheritance) {
    // 可排序表头是 Pointer，但里面的文字要变回 Default 只需要自己写一句。
    SceneNode header(NodeType::Group, "header");
    header.mutableStyle().cursor = CursorShape::Pointer;
    auto label = std::make_unique<SceneNode>(NodeType::Text, "label");
    label->mutableStyle().cursor = CursorShape::Default;
    SceneNode& labelRef = *label;
    header.appendChild(std::move(label));

    EXPECT_EQ(resolveCursor(header), CursorShape::Pointer);
    EXPECT_EQ(resolveCursor(labelRef), CursorShape::Default);
}

}  // namespace
