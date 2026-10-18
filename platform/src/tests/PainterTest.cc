// Painter 的像素级验证：用 SDL 的软件渲染器画到内存表面，再读回像素。
// 这样不需要窗口 / WebGL，node 里也能跑。

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>

#include <SDL3/SDL.h>

#include "kiriyama/core/Color.h"
#include "kiriyama/render/Painter.h"
#include "kiriyama/render/SdlColor.h"

namespace kiriyama::render {
namespace {

using core::Color;
using core::Rgba8;
using core::RectD;
using core::Vec2d;

constexpr Color kWhite = Color::Hex(0xFFFFFF);
constexpr Color kRed = Color::Hex(0xE03030);
constexpr Color kGreen = Color::Hex(0x2E8A85);

class PainterTest : public ::testing::Test {
protected:
    void SetUp() override {
        surface_ = SDL_CreateSurface(kSize, kSize, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface_, nullptr);
        renderer_ = SDL_CreateSoftwareRenderer(surface_);
        ASSERT_NE(renderer_, nullptr);
        painter_ = std::make_unique<Painter>(renderer_);
        painter_->color(kWhite).clear();  // 起点统一是白底，读像素才有确定答案
    }

    void TearDown() override {
        painter_.reset();
        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
        }
        if (surface_) {
            SDL_DestroySurface(surface_);
        }
    }

    [[nodiscard]] Rgba8 pixel(int x, int y) const {
        // SDL3 的渲染器是命令队列，软件后端也要 Present 一次才写回 surface。
        SDL_RenderPresent(renderer_);
        Uint8 r = 0;
        Uint8 g = 0;
        Uint8 b = 0;
        Uint8 a = 0;
        EXPECT_TRUE(SDL_ReadSurfacePixel(surface_, x, y, &r, &g, &b, &a));
        return Rgba8{r, g, b, a};
    }

    /// 区域内是否有任何像素接近 target——避开浮点坐标落在哪一像素行的抖动。
    [[nodiscard]] bool anyNear(const RectD& area, const Rgba8& target, int tolerance = 8) const {
        for (int y = static_cast<int>(area.top()); y < static_cast<int>(area.bottom()); ++y) {
            for (int x = static_cast<int>(area.left()); x < static_cast<int>(area.right()); ++x) {
                const Rgba8 p = pixel(x, y);
                if (std::abs(p.r - target.r) <= tolerance && std::abs(p.g - target.g) <= tolerance &&
                    std::abs(p.b - target.b) <= tolerance) {
                    return true;
                }
            }
        }
        return false;
    }

    static constexpr int kSize = 64;

    SDL_Surface* surface_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    std::unique_ptr<Painter> painter_;
};

TEST_F(PainterTest, SettersAreChainable) {
    Painter& p = *painter_;
    EXPECT_EQ(&p.color(kRed), &p);
    EXPECT_EQ(&p.color(kRed, 0.5), &p);
    EXPECT_EQ(&p.opacity(0.25), &p);
    EXPECT_EQ(&p.strokeWidth(3.0), &p);
    EXPECT_EQ(&p.reset(), &p);
    EXPECT_EQ(&p.fillRect(RectD{0, 0, 4, 4}), &p);
    EXPECT_EQ(&p.strokeRect(RectD{0, 0, 4, 4}), &p);
    EXPECT_EQ(&p.line({0, 0}, {4, 4}), &p);
    EXPECT_EQ(&p.clear(), &p);
}

TEST_F(PainterTest, ResetRestoresDefaults) {
    painter_->color(kRed, 0.3).strokeWidth(5.0).reset();
    EXPECT_EQ(painter_->color(), Color{});
    EXPECT_DOUBLE_EQ(painter_->opacity(), 1.0);
    EXPECT_DOUBLE_EQ(painter_->strokeWidth(), 1.0);
}

TEST_F(PainterTest, OpacityIsClamped) {
    painter_->opacity(5.0);
    EXPECT_DOUBLE_EQ(painter_->opacity(), 1.0);
    painter_->opacity(-2.0);
    EXPECT_DOUBLE_EQ(painter_->opacity(), 0.0);
}

TEST_F(PainterTest, StrokeWidthNeverDropsToZero) {
    painter_->strokeWidth(0.0);
    EXPECT_DOUBLE_EQ(painter_->strokeWidth(), 1.0);
}

TEST_F(PainterTest, ClearFillsWholeTarget) {
    painter_->color(Color::Hex(0x102030)).clear();
    const Rgba8 expected{0x10, 0x20, 0x30, 0xFF};
    EXPECT_EQ(pixel(0, 0), expected);
    EXPECT_EQ(pixel(kSize - 1, kSize - 1), expected);
}

TEST_F(PainterTest, FillRectLeavesOutsideUntouched) {
    painter_->color(kGreen).fillRect(RectD{8, 8, 16, 16});
    EXPECT_EQ(pixel(12, 12), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));
    EXPECT_EQ(pixel(2, 2), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(pixel(30, 30), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST_F(PainterTest, OpacityBlendsOverBackground) {
    painter_->color(Color::Hex(0x000000), 0.5).fillRect(RectD{0, 0, kSize, kSize});
    const Rgba8 p = pixel(32, 32);
    // 黑 50% 叠在白底上 ≈ 128 灰
    EXPECT_NEAR(p.r, 128, 2);
    EXPECT_NEAR(p.g, 128, 2);
    EXPECT_NEAR(p.b, 128, 2);
    EXPECT_EQ(p.a, 255);
}

TEST_F(PainterTest, StrokeRectDrawsBorderButNotCenter) {
    painter_->color(kRed).strokeRect(RectD{10, 10, 20, 20});
    EXPECT_TRUE(anyNear(RectD{8, 8, 6, 6}, (Rgba8{0xE0, 0x30, 0x30, 0xFF}), 24));
    EXPECT_EQ(pixel(20, 20), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));
}

// 回归：strokeRoundRect 曾经漏了 applyColor()，描边沿用上一次残留的颜色——
// 同色的填充之后画同色描边就完全看不出来（SparkBar 的 hover 边框踩过）。
TEST_F(PainterTest, StrokeRoundRectUsesCurrentColor) {
    const RectD rect{16.0, 16.0, 32.0, 32.0};
    painter_->color(kGreen).fillRoundRect(rect, 8.0);      // 让"上一次的颜色"是绿
    painter_->color(kRed).strokeWidth(3.0).strokeRoundRect(rect, 8.0);

    const Rgba8 red{0xE0, 0x30, 0x30, 0xFF};
    const Rgba8 green{0x2E, 0x8A, 0x85, 0xFF};
    EXPECT_TRUE(anyNear(RectD{15, 30, 2, 4}, red, 24));    // 左边框必须是当前色（红）
    EXPECT_FALSE(anyNear(RectD{15, 30, 2, 4}, green, 8));  // 不能是残留的绿
    EXPECT_TRUE(anyNear(RectD{30, 30, 4, 4}, green, 8));   // 中心仍是绿填充
}

TEST_F(PainterTest, ThickLineCoversSeveralRows) {    painter_->color(kRed).strokeWidth(4.0).line({4, 32.5}, {60, 32.5});
    EXPECT_TRUE(anyNear(RectD{20, 30, 4, 2}, (Rgba8{0xE0, 0x30, 0x30, 0xFF}), 24));
    EXPECT_TRUE(anyNear(RectD{20, 33, 4, 2}, (Rgba8{0xE0, 0x30, 0x30, 0xFF}), 24));
    EXPECT_EQ(pixel(20, 26), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));  // 没粗到 6px
}

TEST_F(PainterTest, FillCircleHitsCenterMissesCorners) {
    painter_->color(kGreen).fillCircle({32.0, 32.0}, 20.0);
    EXPECT_EQ(pixel(32, 32), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));
    EXPECT_EQ(pixel(32, 20), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));
    EXPECT_EQ(pixel(32, 4), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));   // 圆心正上方，圆外
    EXPECT_EQ(pixel(4, 32), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST_F(PainterTest, FillPolygonFillsInsideOnly) {
    const Vec2d triangle[] = {{32.0, 10.0}, {50.0, 50.0}, {14.0, 50.0}};
    painter_->color(kGreen).fillPolygon(triangle);
    EXPECT_EQ(pixel(32, 40), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));
    EXPECT_EQ(pixel(4, 4), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(pixel(56, 56), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));
}

// 回归：fillPolygon 曾经只有三角扇——凸多边形没问题，凹多边形会被多画
// （扇环扫角超过 180° 的甜甜圈扇区，内圈的洞被填掉一块，画面出现"白楔子"）。
TEST_F(PainterTest, FillPolygonHandlesConcave) {
    // L 形（凹）：三角扇从 {8,8} 扇开会把右上缺口填上；耳切必须留白。
    const Vec2d lShape[] = {{8.0, 8.0}, {40.0, 8.0}, {40.0, 16.0},
                            {16.0, 16.0}, {16.0, 40.0}, {8.0, 40.0}};
    painter_->color(kGreen).fillPolygon(lShape);
    const Rgba8 white{0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(pixel(24, 12), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));  // 上横条
    EXPECT_EQ(pixel(12, 24), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));  // 左竖条
    EXPECT_EQ(pixel(28, 28), white);                            // 凹口必须留白
    EXPECT_EQ(pixel(12, 12), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));  // 拐角内侧

    // 甜甜圈扇环：内半径 8、外半径 24，扫 270°——内圈的洞必须留白。
    const Vec2d center{32.0, 32.0};
    std::vector<Vec2d> ring;
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    for (int i = 0; i <= 27; ++i) {
        const double a = -90.0 * kDeg + 270.0 * kDeg * (static_cast<double>(i) / 27.0);
        ring.push_back({center.x + std::cos(a) * 24.0, center.y + std::sin(a) * 24.0});
    }
    for (int i = 27; i >= 0; --i) {
        const double a = -90.0 * kDeg + 270.0 * kDeg * (static_cast<double>(i) / 27.0);
        ring.push_back({center.x + std::cos(a) * 8.0, center.y + std::sin(a) * 8.0});
    }
    painter_->color(kGreen).fillPolygon(ring);
    EXPECT_EQ(pixel(32, 14), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));  // 外环上缘
    EXPECT_EQ(pixel(32, 28), white);                            // 内圈洞里必须留白
}

TEST_F(PainterTest, RoundRectClipsCorners) {
    painter_->color(kGreen).fillRoundRect(RectD{8, 8, 40, 40}, 12.0);
    EXPECT_EQ(pixel(28, 28), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));  // 中心
    EXPECT_EQ(pixel(9, 9), (Rgba8{0xFF, 0xFF, 0xFF, 0xFF}));     // 圆角外的直角
}

TEST_F(PainterTest, ChainedDrawKeepsState) {
    Painter& p = *painter_;
    p.color(kGreen).fillRect(RectD{8, 8, 16, 16}).color(kRed).strokeWidth(3.0).strokeRect(
        RectD{8, 8, 16, 16});
    EXPECT_EQ(pixel(16, 16), (Rgba8{0x2E, 0x8A, 0x85, 0xFF}));   // 填充还在
    EXPECT_TRUE(anyNear(RectD{6, 6, 6, 6}, (Rgba8{0xE0, 0x30, 0x30, 0xFF}), 24));  // 描边盖上去了
    EXPECT_EQ(painter_->color(), kRed);  // 状态是最后设置的那个
}

TEST(SdlColorConversion, RoundsAndClamps) {
    const SDL_Color c = toSdlColor(Color::Hex(0x3E6DB5), 0.5);
    EXPECT_EQ(c.r, 0x3E);
    EXPECT_EQ(c.g, 0x6D);
    EXPECT_EQ(c.b, 0xB5);
    EXPECT_NEAR(c.a, 128, 1);

    const SDL_Color fullyOpaque = toSdlColor(Color::Hex(0x3E6DB5), 9.0);  // opacity 被夹到 1
    EXPECT_EQ(fullyOpaque.a, 255);
}

TEST(SdlColorConversion, RoundTripsThroughCore) {
    const Color original = Color::Hex(0x2C2C2A80u);
    EXPECT_EQ(fromSdlColor(toSdlColor(original)), original);
}

}  // namespace
}  // namespace kiriyama::render
