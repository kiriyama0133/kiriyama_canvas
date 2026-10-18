#include <memory>

#include <gtest/gtest.h>

#include "kiriyama/render/App.h"

namespace {

using kiriyama::core::Color;
using kiriyama::render::App;
using kiriyama::render::AppConfig;
using kiriyama::render::AppDelegate;

/// 最小 delegate：只改配置，不画东西。用来验证宿主把配置透传对了。
class ProbeDelegate final : public AppDelegate {
public:
    [[nodiscard]] AppConfig config() const override {
        AppConfig config;
        config.title = "probe";
        config.logicalWidth = 640;
        config.logicalHeight = 360;
        config.clearColor = Color::Hex(0x123456);
        return config;
    }

    void onFrame(App&) override {}
};

TEST(AppHost, ReadsDelegateConfigBeforeStart) {
    App app(std::make_unique<ProbeDelegate>());

    EXPECT_EQ(app.config().title, "probe");
    EXPECT_EQ(app.config().logicalWidth, 640);
    EXPECT_EQ(app.config().logicalHeight, 360);
    EXPECT_EQ(app.config().clearColor, Color::Hex(0x123456));

    // 没 start 就没有窗口，但配置和画布已经可读。
    EXPECT_FALSE(app.running());
    EXPECT_EQ(app.window(), nullptr);
    EXPECT_EQ(app.renderer(), nullptr);
}

TEST(AppHost, CanvasMatchesLogicalSpace) {
    App app(std::make_unique<ProbeDelegate>());
    const auto canvas = app.canvas();

    EXPECT_DOUBLE_EQ(canvas.x, 0.0);
    EXPECT_DOUBLE_EQ(canvas.y, 0.0);
    EXPECT_DOUBLE_EQ(canvas.width, 640.0);
    EXPECT_DOUBLE_EQ(canvas.height, 360.0);
}

TEST(AppHost, DefaultsAreUsableWithoutDelegateOverrides) {
    struct PlainDelegate final : AppDelegate {
        void onFrame(App&) override {}
    };

    App app(std::make_unique<PlainDelegate>());
    EXPECT_FALSE(app.config().title.empty());
    EXPECT_GT(app.config().logicalWidth, 0);
    EXPECT_GT(app.config().logicalHeight, 0);
    EXPECT_DOUBLE_EQ(app.canvas().width, static_cast<double>(app.config().logicalWidth));
}

TEST(AppHost, StopIsIdempotentBeforeStart) {
    App app(std::make_unique<ProbeDelegate>());
    app.stop();
    app.stop();
    EXPECT_FALSE(app.running());
}

}  // namespace
