#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "kiriyama/core/Chart.h"

namespace {

using kiriyama::core::findByKey;
using kiriyama::core::NodeType;
using kiriyama::core::RectD;
using kiriyama::core::reconcileScene;
using kiriyama::core::sameProperties;
using kiriyama::core::SceneNode;
using kiriyama::core::SceneStyle;

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

std::unique_ptr<SceneNode> makeGroup(const std::string& key) {
    auto node = std::make_unique<SceneNode>(NodeType::Group, key);
    return node;
}

/// root > [a, b]，项高固定，方便断言
std::unique_ptr<SceneNode> makeSimpleTree() {
    auto root = makeGroup("root");
    root->appendChild(makeRect("a", RectD{0.0, 0.0, 10.0, 10.0}));
    root->appendChild(makeRect("b", RectD{0.0, 20.0, 10.0, 10.0}));
    return root;
}

std::vector<std::string> childKeys(const SceneNode& node) {
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < node.childCount(); ++i) {
        keys.push_back(node.childAt(i)->key());
    }
    return keys;
}

std::uint64_t idOfKey(const SceneNode& root, const std::string& key) {
    const SceneNode* node = findByKey(root, key);
    return node == nullptr ? 0 : node->id();
}

// ---------------------------------------------------------------- 属性

TEST(SceneDiffProperties, SamePropertiesIgnoresKeyAndName) {
    auto lhs = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});
    auto rhs = makeRect("b", RectD{0.0, 0.0, 10.0, 10.0});
    rhs->setName("另一个名字");
    EXPECT_TRUE(sameProperties(*lhs, *rhs));

    rhs->setFrame(RectD{0.0, 0.0, 11.0, 10.0});
    EXPECT_FALSE(sameProperties(*lhs, *rhs));
}

TEST(SceneDiffProperties, DetectsEachPropertyKind) {
    auto base = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});

    auto visible = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});
    visible->setVisible(false);
    EXPECT_FALSE(sameProperties(*base, *visible));

    auto styled = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});
    SceneStyle other = filledStyle();
    other.strokeWidth = 2.0;
    styled->setStyle(other);
    EXPECT_FALSE(sameProperties(*base, *styled));

    auto moved = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});
    moved->setTranslation({1.0, 0.0});
    EXPECT_FALSE(sameProperties(*base, *moved));

    auto typed = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});
    typed->setType(NodeType::Ellipse);
    EXPECT_FALSE(sameProperties(*base, *typed));

    auto textual = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});
    textual->setText("x");
    EXPECT_FALSE(sameProperties(*base, *textual));

    auto pointed = makeRect("a", RectD{0.0, 0.0, 10.0, 10.0});
    pointed->setPoints({{0.0, 0.0}});
    EXPECT_FALSE(sameProperties(*base, *pointed));
}

// ---------------------------------------------------------------- 无变化

TEST(SceneDiff, IdenticalTreesProduceNoChanges) {
    auto target = makeSimpleTree();
    auto source = makeSimpleTree();
    target->clearSubtreeDirty();

    const auto diff = reconcileScene(*target, *source);

    EXPECT_FALSE(diff.stats.changed());
    EXPECT_EQ(diff.stats.updated, 0u);
    EXPECT_EQ(diff.stats.inserted, 0u);
    EXPECT_EQ(diff.stats.removed, 0u);
    EXPECT_EQ(diff.stats.moved, 0u);
    EXPECT_EQ(diff.stats.reused, 2u);
    EXPECT_TRUE(diff.changed.empty());

    // 关键性质：一次没有变化的对账不应该把树标脏
    EXPECT_FALSE(target->hasDirtyInSubtree());
}

TEST(SceneDiff, PropertyChangeReusesNodeAndMarksDirty) {
    auto target = makeSimpleTree();
    auto source = makeSimpleTree();
    SceneNode* a = findByKey(*target, "a");
    const std::uint64_t idBefore = a->id();
    target->clearSubtreeDirty();

    source->childAt(0)->setFrame(RectD{0.0, 0.0, 99.0, 10.0});
    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.updated, 1u);
    EXPECT_EQ(diff.stats.inserted, 0u);
    EXPECT_EQ(diff.stats.removed, 0u);
    ASSERT_EQ(diff.changed.size(), 1u);
    EXPECT_EQ(diff.changed[0], a);
    EXPECT_EQ(diff.changed[0]->id(), idBefore);  // 身份没变
    EXPECT_EQ(a->frame(), (RectD{0.0, 0.0, 99.0, 10.0}));
    EXPECT_TRUE(a->dirty());
    EXPECT_TRUE(target->hasDirtyInSubtree());
}

TEST(SceneDiff, MultiplePropertyChangesAreAllReported) {
    auto target = makeSimpleTree();
    auto source = makeSimpleTree();

    source->childAt(0)->setFrame(RectD{0.0, 0.0, 1.0, 1.0});
    source->childAt(1)->setTranslation({5.0, 5.0});
    source->setTranslation({100.0, 0.0});

    const auto diff = reconcileScene(*target, *source);
    EXPECT_EQ(diff.stats.updated, 3u);
    EXPECT_EQ(diff.changed.size(), 3u);
}

// ---------------------------------------------------------------- 增删

TEST(SceneDiff, InsertChild) {
    auto target = makeSimpleTree();
    auto source = makeSimpleTree();
    source->appendChild(makeRect("c", RectD{0.0, 40.0, 10.0, 10.0}));

    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.inserted, 1u);
    EXPECT_EQ(diff.stats.removed, 0u);
    EXPECT_EQ(diff.stats.reused, 2u);
    ASSERT_EQ(target->childCount(), 3u);
    EXPECT_EQ(childKeys(*target), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(target->childAt(2)->parent(), target.get());
    EXPECT_EQ(diff.changed.size(), 1u);
}

TEST(SceneDiff, InsertCountsWholeSubtree) {
    auto target = makeSimpleTree();
    auto source = makeSimpleTree();

    auto group = makeGroup("g");
    group->appendChild(makeRect("g1", RectD{0.0, 0.0, 5.0, 5.0}));
    group->appendChild(makeRect("g2", RectD{0.0, 10.0, 5.0, 5.0}));
    source->appendChild(std::move(group));

    const auto diff = reconcileScene(*target, *source);
    EXPECT_EQ(diff.stats.inserted, 3u);  // 组 + 两个子节点
    ASSERT_EQ(target->childCount(), 3u);
    EXPECT_EQ(target->childAt(2)->childCount(), 2u);
}

TEST(SceneDiff, InsertedSubtreeIsClonedNotShared) {
    auto target = makeSimpleTree();
    auto source = makeSimpleTree();
    source->appendChild(makeRect("c", RectD{0.0, 40.0, 10.0, 10.0}));

    const std::uint64_t sourceId = findByKey(*source, "c")->id();
    const auto diff = reconcileScene(*target, *source);
    EXPECT_EQ(diff.stats.inserted, 1u);

    SceneNode* inserted = findByKey(*target, "c");
    ASSERT_NE(inserted, nullptr);
    EXPECT_NE(inserted->id(), sourceId);
    EXPECT_NE(inserted, findByKey(*source, "c"));
}

TEST(SceneDiff, RemoveChild) {
    auto target = makeSimpleTree();
    auto source = makeGroup("root");
    source->appendChild(makeRect("a", RectD{0.0, 0.0, 10.0, 10.0}));

    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.removed, 1u);
    EXPECT_EQ(diff.stats.reused, 1u);
    ASSERT_EQ(target->childCount(), 1u);
    EXPECT_EQ(childKeys(*target), (std::vector<std::string>{"a"}));
}

TEST(SceneDiff, RemoveWholeSubtree) {
    auto target = makeSimpleTree();
    auto group = makeGroup("g");
    group->appendChild(makeRect("g1", RectD{0.0, 0.0, 5.0, 5.0}));
    group->appendChild(makeRect("g2", RectD{0.0, 10.0, 5.0, 5.0}));
    target->appendChild(std::move(group));

    auto source = makeSimpleTree();
    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.removed, 3u);
    EXPECT_EQ(target->childCount(), 2u);
}

TEST(SceneDiff, RemoveEverything) {
    auto target = makeSimpleTree();
    auto source = makeGroup("root");

    const auto diff = reconcileScene(*target, *source);
    EXPECT_EQ(diff.stats.removed, 2u);
    EXPECT_EQ(target->childCount(), 0u);
}

// ---------------------------------------------------------------- 重排

TEST(SceneDiff, ReorderKeyedChildrenKeepsIdentity) {
    auto target = makeSimpleTree();
    target->appendChild(makeRect("c", RectD{0.0, 40.0, 10.0, 10.0}));

    const std::uint64_t idA = idOfKey(*target, "a");
    const std::uint64_t idB = idOfKey(*target, "b");
    const std::uint64_t idC = idOfKey(*target, "c");

    auto source = makeGroup("root");
    source->appendChild(makeRect("c", RectD{0.0, 0.0, 10.0, 10.0}));
    source->appendChild(makeRect("b", RectD{0.0, 20.0, 10.0, 10.0}));
    source->appendChild(makeRect("a", RectD{0.0, 40.0, 10.0, 10.0}));

    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.moved, 2u);  // 3 项倒序，最少移动 2 次
    EXPECT_EQ(diff.stats.inserted, 0u);
    EXPECT_EQ(diff.stats.removed, 0u);
    EXPECT_EQ(diff.stats.reused, 3u);
    EXPECT_EQ(childKeys(*target), (std::vector<std::string>{"c", "b", "a"}));
    EXPECT_EQ(idOfKey(*target, "a"), idA);
    EXPECT_EQ(idOfKey(*target, "b"), idB);
    EXPECT_EQ(idOfKey(*target, "c"), idC);
}

TEST(SceneDiff, DeletionDoesNotCountAsMove) {
    auto target = makeSimpleTree();
    target->appendChild(makeRect("c", RectD{0.0, 40.0, 10.0, 10.0}));

    auto source = makeGroup("root");
    source->appendChild(makeRect("b", RectD{0.0, 20.0, 10.0, 10.0}));
    source->appendChild(makeRect("c", RectD{0.0, 40.0, 10.0, 10.0}));

    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.removed, 1u);
    EXPECT_EQ(diff.stats.moved, 0u);  // 下标整体前移不算移动
    EXPECT_EQ(childKeys(*target), (std::vector<std::string>{"b", "c"}));
}

TEST(SceneDiff, ReorderPlusInsert) {
    auto target = makeSimpleTree();
    auto source = makeGroup("root");
    source->appendChild(makeRect("b", RectD{0.0, 20.0, 10.0, 10.0}));
    source->appendChild(makeRect("a", RectD{0.0, 0.0, 10.0, 10.0}));
    source->appendChild(makeRect("c", RectD{0.0, 40.0, 10.0, 10.0}));

    const auto diff = reconcileScene(*target, *source);
    EXPECT_EQ(diff.stats.moved, 1u);
    EXPECT_EQ(diff.stats.inserted, 1u);
    EXPECT_EQ(childKeys(*target), (std::vector<std::string>{"b", "a", "c"}));
}

// ---------------------------------------------------------------- 替换

TEST(SceneDiff, TypeChangeReplacesNode) {
    auto target = makeGroup("root");
    target->appendChild(makeRect("x", RectD{0.0, 0.0, 10.0, 10.0}));
    const std::uint64_t oldId = idOfKey(*target, "x");

    auto source = makeGroup("root");
    auto ellipse = std::make_unique<SceneNode>(NodeType::Ellipse, "x");
    ellipse->setFrame(RectD{0.0, 0.0, 10.0, 10.0});
    ellipse->setStyle(filledStyle());
    source->appendChild(std::move(ellipse));

    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.inserted, 1u);
    EXPECT_EQ(diff.stats.removed, 1u);
    EXPECT_EQ(diff.stats.reused, 0u);
    ASSERT_EQ(target->childCount(), 1u);
    EXPECT_EQ(target->childAt(0)->type(), NodeType::Ellipse);
    EXPECT_NE(target->childAt(0)->id(), oldId);
}

TEST(SceneDiff, KeyMismatchMeansReplace) {
    auto target = makeGroup("root");
    target->appendChild(makeRect("a", RectD{0.0, 0.0, 10.0, 10.0}));

    auto source = makeGroup("root");
    source->appendChild(makeRect("z", RectD{0.0, 0.0, 10.0, 10.0}));

    const auto diff = reconcileScene(*target, *source);
    EXPECT_EQ(diff.stats.inserted, 1u);
    EXPECT_EQ(diff.stats.removed, 1u);
    EXPECT_EQ(target->childAt(0)->key(), "z");
}

// ---------------------------------------------------------------- 无 key

TEST(SceneDiff, UnkeyedChildrenMatchByPosition) {
    auto target = makeGroup("root");
    target->appendChild(makeRect("", RectD{0.0, 0.0, 10.0, 10.0}));
    target->appendChild(makeRect("", RectD{0.0, 20.0, 10.0, 10.0}));
    SceneNode* second = target->childAt(1);
    const std::uint64_t secondId = second->id();

    auto source = makeGroup("root");
    source->appendChild(makeRect("", RectD{0.0, 0.0, 10.0, 10.0}));
    source->appendChild(makeRect("", RectD{0.0, 30.0, 10.0, 10.0}));

    const auto diff = reconcileScene(*target, *source);
    EXPECT_EQ(diff.stats.reused, 2u);
    EXPECT_EQ(diff.stats.updated, 1u);
    EXPECT_EQ(diff.stats.inserted, 0u);
    EXPECT_EQ(diff.stats.removed, 0u);
    EXPECT_EQ(target->childAt(1)->id(), secondId);
    EXPECT_EQ(target->childAt(1)->frame(), (RectD{0.0, 30.0, 10.0, 10.0}));
}

TEST(SceneDiff, UnkeyedChildrenRespectType) {
    auto target = makeGroup("root");
    target->appendChild(makeRect("", RectD{0.0, 0.0, 10.0, 10.0}));

    auto source = makeGroup("root");
    source->appendChild(std::make_unique<SceneNode>(NodeType::Text, ""));
    source->childAt(0)->setFrame(RectD{0.0, 0.0, 10.0, 10.0});

    const auto diff = reconcileScene(*target, *source);
    // 类型不同，不能按位置复用
    EXPECT_EQ(diff.stats.inserted, 1u);
    EXPECT_EQ(diff.stats.removed, 1u);
}

// ---------------------------------------------------------------- 深层

TEST(SceneDiff, DeepNestedUpdate) {
    auto target = makeGroup("root");
    auto group = makeGroup("g");
    group->appendChild(makeRect("r", RectD{0.0, 0.0, 10.0, 10.0}));
    target->appendChild(std::move(group));
    target->clearSubtreeDirty();

    auto source = makeGroup("root");
    auto sourceGroup = makeGroup("g");
    sourceGroup->appendChild(makeRect("r", RectD{0.0, 0.0, 42.0, 10.0}));
    source->appendChild(std::move(sourceGroup));

    SceneNode* inner = findByKey(*target, "r");
    const auto diff = reconcileScene(*target, *source);

    EXPECT_EQ(diff.stats.updated, 1u);
    EXPECT_EQ(diff.stats.reused, 2u);
    ASSERT_EQ(diff.changed.size(), 1u);
    EXPECT_EQ(diff.changed[0], inner);

    // 只有内层节点脏，中间的分组节点自身没变
    EXPECT_TRUE(inner->dirty());
    EXPECT_FALSE(findByKey(*target, "g")->dirty());
    EXPECT_TRUE(target->hasDirtyInSubtree());
}

TEST(SceneDiff, RootTypeChangeKeepsRootIdentity) {
    SceneNode target(NodeType::Group, "root");
    SceneNode source(NodeType::Rect, "root");

    const std::uint64_t before = target.id();
    const auto diff = reconcileScene(target, source);

    EXPECT_EQ(static_cast<int>(target.type()), static_cast<int>(NodeType::Rect));
    EXPECT_EQ(target.id(), before);
    EXPECT_EQ(diff.stats.updated, 1u);
}

TEST(SceneDiff, StatsChangedFlag) {
    kiriyama::core::SceneDiffStats stats;
    EXPECT_FALSE(stats.changed());
    stats.moved = 1;
    EXPECT_TRUE(stats.changed());
}

}  // namespace
