#include <gtest/gtest.h>

#include <memory>

#include "kiriyama/scene/Drag.h"

namespace {

using kiriyama::core::NodeType;
using kiriyama::core::RectD;
using kiriyama::core::SceneNode;
using kiriyama::core::Vec2d;
using kiriyama::scene::Drag;
using kiriyama::scene::DragAxis;
using kiriyama::scene::DragOptions;
using kiriyama::scene::FrameContext;

/// 一个 100x100 的可拖节点，放在原点。
[[nodiscard]] std::unique_ptr<Drag> makeDrag(double x = 0.0, double y = 0.0) {
    auto drag = std::make_unique<Drag>("drag");
    drag->setFrame(RectD{x, y, 100.0, 100.0});
    return drag;
}

[[nodiscard]] FrameContext at(double x, double y, bool down) {
    FrameContext ctx;
    ctx.deltaTime = 1.0 / 60.0;
    ctx.pointer.position = Vec2d{x, y};
    ctx.pointer.down = down;
    return ctx;
}

// ---------------------------------------------------------------- 命中

TEST(Drag, BeginsOnlyInsideNode) {
    auto drag = makeDrag();

    EXPECT_FALSE(drag->begin({150.0, 150.0}));
    EXPECT_FALSE(drag->active());

    EXPECT_TRUE(drag->begin({10.0, 10.0}));
    EXPECT_TRUE(drag->active());
}

// ---------------------------------------------------------------- 位移

TEST(Drag, MovesNodeByPointerDelta) {
    auto drag = makeDrag();

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    EXPECT_TRUE(drag->move({30.0, 25.0}));

    EXPECT_NEAR(drag->transform().m02, 20.0, 1e-9);
    EXPECT_NEAR(drag->transform().m12, 15.0, 1e-9);
    EXPECT_NEAR(drag->delta().x, 20.0, 1e-9);
    EXPECT_NEAR(drag->delta().y, 15.0, 1e-9);
    EXPECT_TRUE(drag->dragging());
}

TEST(Drag, AxisLockKeepsOtherAxisStill) {
    auto drag = makeDrag();
    DragOptions options;
    options.axis = DragAxis::X;
    drag->setOptions(options);

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    drag->move({60.0, 90.0});

    EXPECT_NEAR(drag->transform().m02, 50.0, 1e-9);
    EXPECT_NEAR(drag->transform().m12, 0.0, 1e-9);
}

TEST(Drag, ThresholdSwallowsSmallMoves) {
    auto drag = makeDrag();
    DragOptions options;
    options.threshold = 5.0;
    drag->setOptions(options);

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    EXPECT_FALSE(drag->move({12.0, 12.0}));
    EXPECT_FALSE(drag->dragging());
    EXPECT_NEAR(drag->transform().m02, 0.0, 1e-9);

    EXPECT_TRUE(drag->move({20.0, 10.0}));
    EXPECT_TRUE(drag->dragging());
    EXPECT_NEAR(drag->transform().m02, 10.0, 1e-9);
}

TEST(Drag, ClampsToBounds) {
    auto drag = makeDrag();
    DragOptions options;
    options.bounds = RectD{0.0, 0.0, 200.0, 200.0};
    drag->setOptions(options);

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    drag->move({600.0, 600.0});

    // 节点 100x100，活动区 200x200 -> 最多只能走 100
    EXPECT_NEAR(drag->transform().m02, 100.0, 1e-9);
    EXPECT_NEAR(drag->transform().m12, 100.0, 1e-9);
}

TEST(Drag, ClampsBackwardsToo) {
    auto drag = makeDrag();
    drag->setTranslation({50.0, 50.0});
    DragOptions options;
    options.bounds = RectD{0.0, 0.0, 200.0, 200.0};
    drag->setOptions(options);

    ASSERT_TRUE(drag->begin({60.0, 60.0}));
    drag->move({-500.0, -500.0});

    EXPECT_NEAR(drag->transform().m02, 0.0, 1e-9);
    EXPECT_NEAR(drag->transform().m12, 0.0, 1e-9);
}

TEST(Drag, WorksInsideTranslatedParent) {
    // 父组整体右移 50，指针的屏幕坐标和父坐标差着 50，换算错了就会跳一下
    SceneNode parent(NodeType::Group);
    parent.setTranslation({50.0, 0.0});

    auto drag = makeDrag();
    Drag* raw = drag.get();
    parent.appendChild(std::move(drag));

    ASSERT_TRUE(raw->begin({60.0, 10.0}));
    ASSERT_TRUE(raw->move({80.0, 10.0}));

    EXPECT_NEAR(raw->transform().m02, 20.0, 1e-9);
    EXPECT_NEAR(raw->transform().m12, 0.0, 1e-9);
}

TEST(Drag, EndReportsWhetherItReallyDragged) {
    auto drag = makeDrag();
    DragOptions options;
    options.threshold = 5.0;
    drag->setOptions(options);

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    EXPECT_FALSE(drag->end({11.0, 11.0}));  // 只是点了一下
    EXPECT_FALSE(drag->active());

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    drag->move({40.0, 10.0});
    EXPECT_TRUE(drag->end({40.0, 10.0}));
    EXPECT_FALSE(drag->active());
}

TEST(Drag, BeginTwiceRestartsFromNewGrabPoint) {
    auto drag = makeDrag();

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    drag->move({50.0, 50.0});
    ASSERT_NEAR(drag->transform().m02, 40.0, 1e-9);

    ASSERT_TRUE(drag->begin({50.0, 50.0}));
    drag->move({60.0, 60.0});
    EXPECT_NEAR(drag->transform().m02, 50.0, 1e-9);
}

TEST(Drag, CancelDropsState) {
    auto drag = makeDrag();

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    drag->move({40.0, 40.0});
    drag->cancel();

    EXPECT_FALSE(drag->active());
    EXPECT_FALSE(drag->dragging());
    EXPECT_NEAR(drag->delta().x, 0.0, 1e-9);
    EXPECT_FALSE(drag->hasPointerCapture());
}

TEST(Drag, MoveNodeFalseOnlyReportsDelta) {
    auto drag = makeDrag();
    DragOptions options;
    options.moveNode = false;
    drag->setOptions(options);

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    EXPECT_TRUE(drag->move({40.0, 15.0}));

    EXPECT_NEAR(drag->transform().m02, 0.0, 1e-9);
    EXPECT_NEAR(drag->delta().x, 30.0, 1e-9);
    EXPECT_NEAR(drag->delta().y, 5.0, 1e-9);
}

TEST(Drag, CallbacksFire) {
    auto drag = makeDrag();
    int begins = 0;
    int drags = 0;
    int ends = 0;
    drag->onBegin([&](const Drag&) { ++begins; })
        .onDrag([&](const Drag&) { ++drags; })
        .onEnd([&](const Drag&) { ++ends; });

    ASSERT_TRUE(drag->begin({10.0, 10.0}));
    drag->move({30.0, 10.0});
    drag->end({30.0, 10.0});

    EXPECT_EQ(begins, 1);
    EXPECT_EQ(drags, 1);
    EXPECT_EQ(ends, 1);
}

// ---------------------------------------------------------------- 与基类的配合

TEST(Drag, TickDrivesWholeGesture) {
    auto drag = makeDrag();

    // 第一帧：在内部按下 -> 接管
    drag->tick(at(10.0, 10.0, true));
    EXPECT_TRUE(drag->active());

    // 按住移动 -> 跟着走
    drag->tick(at(40.0, 25.0, true));
    EXPECT_NEAR(drag->transform().m02, 30.0, 1e-9);
    EXPECT_NEAR(drag->transform().m12, 15.0, 1e-9);

    // 松手 -> 结束，位移保留
    drag->tick(at(40.0, 25.0, false));
    EXPECT_FALSE(drag->active());
    EXPECT_NEAR(drag->transform().m02, 30.0, 1e-9);
}

TEST(Drag, TickIgnoresPressOutside) {
    auto drag = makeDrag();

    drag->tick(at(400.0, 400.0, true));
    EXPECT_FALSE(drag->active());

    drag->tick(at(30.0, 30.0, true));
    EXPECT_FALSE(drag->active());  // 按下点在外面，之后挪进来也不算起拖
}

TEST(Drag, TickKeepsDraggingAfterPointerLeaves) {
    auto drag = makeDrag();

    drag->tick(at(10.0, 10.0, true));
    ASSERT_TRUE(drag->active());

    // 拖到节点外面：捕获让 gesture 继续
    drag->tick(at(300.0, 300.0, true));
    EXPECT_TRUE(drag->active());
    EXPECT_TRUE(drag->hasPointerCapture());
    EXPECT_NEAR(drag->transform().m02, 290.0, 1e-9);

    drag->tick(at(300.0, 300.0, false));
    EXPECT_FALSE(drag->active());
    EXPECT_FALSE(drag->hasPointerCapture());
}

}  // namespace
