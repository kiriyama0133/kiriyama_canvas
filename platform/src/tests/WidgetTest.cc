#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "kiriyama/scene/Widget.h"

namespace {

using kiriyama::core::NodeType;
using kiriyama::core::RectD;
using kiriyama::core::SceneNode;
using kiriyama::core::Vec2d;
using kiriyama::scene::FrameContext;
using kiriyama::scene::Widget;

/// 把所有回调记下来的探针。
class Probe : public Widget {
public:
    explicit Probe(std::string key = {}) : Widget(std::move(key)) {}

    int enters{0};
    int leaves{0};
    int presses{0};
    int releases{0};
    int updates{0};
    bool releasedInside{false};

protected:
    void onUpdate(const FrameContext&) override { ++updates; }
    void onPointerEnter() override { ++enters; }
    void onPointerLeave() override { ++leaves; }
    void onPress() override { ++presses; }
    void onRelease(bool inside) override {
        ++releases;
        releasedInside = inside;
    }
};

[[nodiscard]] FrameContext at(double x, double y, bool down) {
    FrameContext ctx;
    ctx.time = 1.0;
    ctx.deltaTime = 1.0 / 60.0;
    ctx.pointer.position = Vec2d{x, y};
    ctx.pointer.down = down;
    return ctx;
}

// ---------------------------------------------------------------- 命中区

TEST(Widget, HitTestUsesFrame) {
    Probe widget;
    widget.setFrame(RectD{10.0, 10.0, 50.0, 50.0});

    EXPECT_TRUE(widget.hitTest({20.0, 20.0}));
    EXPECT_FALSE(widget.hitTest({0.0, 0.0}));
    EXPECT_FALSE(widget.hitTest({100.0, 100.0}));
}

TEST(Widget, HitTestFallsBackToChildrenWhenFrameEmpty) {
    // Group 的 localBounds 恒为空，不能拿它做命中；退化成子树包围盒才对
    Probe widget;
    auto child = std::make_unique<SceneNode>(NodeType::Rect);
    child->setFrame(RectD{5.0, 5.0, 20.0, 20.0});
    widget.appendChild(std::move(child));

    EXPECT_TRUE(widget.hitTest({10.0, 10.0}));
    EXPECT_FALSE(widget.hitTest({50.0, 50.0}));
}

TEST(Widget, HitAreaFollowsTranslation) {
    Probe widget;
    widget.setFrame(RectD{0.0, 0.0, 40.0, 40.0});
    EXPECT_TRUE(widget.hitTest({10.0, 10.0}));

    widget.setTranslation({100.0, 0.0});
    EXPECT_FALSE(widget.hitTest({10.0, 10.0}));
    EXPECT_TRUE(widget.hitTest({110.0, 10.0}));
}

// ---------------------------------------------------------------- 指针状态

TEST(Widget, EnterAndLeaveHooks) {
    Probe widget;
    widget.setFrame(RectD{0.0, 0.0, 100.0, 100.0});

    widget.tick(at(10.0, 10.0, false));
    EXPECT_EQ(widget.enters, 1);
    EXPECT_TRUE(widget.pointerInside());
    EXPECT_TRUE(widget.hovered());

    widget.tick(at(200.0, 200.0, false));
    EXPECT_EQ(widget.leaves, 1);
    EXPECT_FALSE(widget.pointerInside());

    // 一直在外面移动不应该反复触发 leave
    widget.tick(at(300.0, 200.0, false));
    EXPECT_EQ(widget.leaves, 1);
}

TEST(Widget, ClickInsideFiresPressAndRelease) {
    Probe widget;
    widget.setFrame(RectD{0.0, 0.0, 100.0, 100.0});

    widget.tick(at(10.0, 10.0, false));  // 悬停
    widget.tick(at(10.0, 10.0, true));   // 按下
    EXPECT_EQ(widget.presses, 1);
    EXPECT_TRUE(widget.pressed());

    widget.tick(at(12.0, 12.0, false));  // 原地松开
    EXPECT_EQ(widget.releases, 1);
    EXPECT_TRUE(widget.releasedInside);
    EXPECT_TRUE(widget.clicked());
    EXPECT_FALSE(widget.pressed());

    widget.tick(at(12.0, 12.0, false));
    EXPECT_FALSE(widget.clicked());  // 只在一帧内为真
}

TEST(Widget, PressInsideReleaseOutsideIsNotClick) {
    Probe widget;
    widget.setFrame(RectD{0.0, 0.0, 100.0, 100.0});

    widget.tick(at(30.0, 30.0, true));
    widget.tick(at(300.0, 300.0, false));

    EXPECT_EQ(widget.releases, 1);
    EXPECT_FALSE(widget.releasedInside);
    EXPECT_FALSE(widget.clicked());
}

TEST(Widget, PressOutsideDoesNothing) {
    Probe widget;
    widget.setFrame(RectD{0.0, 0.0, 100.0, 100.0});

    widget.tick(at(500.0, 500.0, true));
    EXPECT_EQ(widget.presses, 0);
    EXPECT_FALSE(widget.pressed());

    // 按着挪进来，也不该补一次 press
    widget.tick(at(50.0, 50.0, true));
    EXPECT_EQ(widget.presses, 0);
}

TEST(Widget, PointerDeltaReportsMovement) {
    Probe widget;
    widget.setFrame(RectD{0.0, 0.0, 400.0, 400.0});

    widget.tick(at(10.0, 10.0, false));
    EXPECT_NEAR(widget.pointerDelta().x, 0.0, 1e-9);  // 第一帧没有参照，位移记 0

    widget.tick(at(25.0, 5.0, false));
    EXPECT_NEAR(widget.pointerDelta().x, 15.0, 1e-9);
    EXPECT_NEAR(widget.pointerDelta().y, -5.0, 1e-9);
}

TEST(Widget, CaptureKeepsPointerInside) {
    Probe widget;
    widget.setFrame(RectD{0.0, 0.0, 100.0, 100.0});

    widget.tick(at(10.0, 10.0, true));
    widget.capturePointer(true);

    widget.tick(at(400.0, 400.0, true));
    EXPECT_TRUE(widget.pointerInside());  // 捕获期间不闪回未悬停态
    EXPECT_EQ(widget.leaves, 0);

    widget.tick(at(400.0, 400.0, false));  // 松手自动释放
    EXPECT_FALSE(widget.hasPointerCapture());
    EXPECT_EQ(widget.leaves, 1);
}

// ---------------------------------------------------------------- 树驱动

TEST(Widget, TickTreeUpdatesNestedWidgets) {
    Probe root("root");
    root.setFrame(RectD{0.0, 0.0, 400.0, 400.0});

    auto child = std::make_unique<Probe>("child");
    child->setFrame(RectD{10.0, 10.0, 50.0, 50.0});
    Probe* childRaw = child.get();
    root.appendChild(std::move(child));

    // 中间夹一个普通节点，也要能穿过去
    auto plain = std::make_unique<SceneNode>(NodeType::Group);
    auto deep = std::make_unique<Probe>("deep");
    deep->setFrame(RectD{0.0, 0.0, 10.0, 10.0});
    Probe* deepRaw = deep.get();
    plain->appendChild(std::move(deep));
    root.appendChild(std::move(plain));

    root.tickTree(at(20.0, 20.0, false));

    EXPECT_EQ(root.updates, 1);
    EXPECT_EQ(childRaw->updates, 1);
    EXPECT_EQ(deepRaw->updates, 1);

    // 父节点先通知：child 在自己的局部坐标里被判定为命中
    EXPECT_TRUE(childRaw->pointerInside());
}

TEST(Widget, BorderAndShadowConfiguration) {
    Probe widget("card");
    widget.setFrame(RectD{0.0, 0.0, 200.0, 100.0});
    EXPECT_FALSE(widget.style().hasStroke());
    EXPECT_FALSE(widget.style().hasShadow());

    widget.setBorder(kiriyama::core::Color::Hex(0x3E6DB5), 2.0, 8.0)
        .setShadow(kiriyama::core::Color::Rgba(0.0, 0.0, 0.0, 0.25), Vec2d{0.0, 6.0}, 12.0, 1.0);

    EXPECT_TRUE(widget.style().hasStroke());
    EXPECT_DOUBLE_EQ(widget.style().strokeWidth, 2.0);
    EXPECT_DOUBLE_EQ(widget.cornerRadius(), 8.0);
    EXPECT_TRUE(widget.style().hasShadow());
    EXPECT_DOUBLE_EQ(widget.shadow().offset.y, 6.0);
    EXPECT_DOUBLE_EQ(widget.shadow().blur, 12.0);
    EXPECT_DOUBLE_EQ(widget.shadow().spread, 1.0);

    widget.clearBorder().clearShadow();
    EXPECT_FALSE(widget.style().hasStroke());
    EXPECT_FALSE(widget.style().hasShadow());
}

}  // namespace
