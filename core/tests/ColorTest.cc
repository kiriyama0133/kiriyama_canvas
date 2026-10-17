#include <gtest/gtest.h>

#include <cstdint>

#include "kiriyama/core/Color.h"

namespace kiriyama::core {
namespace {

TEST(ColorHex, IntegerForm) {
    const Color c = Color::Hex(0xF7F7F5);
    EXPECT_EQ(c, Color::FromBytes(0xF7, 0xF7, 0xF5));
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(ColorHex, IntegerFormWithAlpha) {
    const Color c = Color::Hex(0x3E6DB580u);
    EXPECT_EQ(c.toBytes(), (Rgba8{0x3E, 0x6D, 0xB5, 0x80}));
}

TEST(ColorHex, StringFormsWithHash) {
    EXPECT_EQ(Color::FromHex("#F7F7F5"), Color::Hex(0xF7F7F5));
    EXPECT_EQ(Color::FromHex("#f7f7f5"), Color::Hex(0xF7F7F5));
    EXPECT_EQ(Color::FromHex("#F7F7F5AA"), Color::Hex(0xF7F7F5AAu));
}

TEST(ColorHex, StringFormWithoutHash) {
    EXPECT_EQ(Color::FromHex("2E8A85"), Color::Hex(0x2E8A85));
}

TEST(ColorHex, ShortFormExpandsEachNibble) {
    EXPECT_EQ(Color::FromHex("#FFF"), Color::FromBytes(0xFF, 0xFF, 0xFF));
    EXPECT_EQ(Color::FromHex("#48C"), Color::FromBytes(0x44, 0x88, 0xCC));
    EXPECT_EQ(Color::FromHex("#48C8"), Color::FromBytes(0x44, 0x88, 0xCC, 0x88));
}

TEST(ColorHex, InvalidInputIsTransparent) {
    EXPECT_FALSE(Color::FromHex("#GGGGGG").visible());
    EXPECT_FALSE(Color::FromHex("#12345").visible());
    EXPECT_FALSE(Color::FromHex("").visible());
    EXPECT_FALSE(Color::TryParseHex("#GGGGGG").has_value());
    EXPECT_FALSE(Color::TryParseHex("#1234567").has_value());
    EXPECT_TRUE(Color::TryParseHex("#123456").has_value());
}

TEST(ColorHex, MatchesByteEquivalent) {
    // 同一个色值的三条入口必须给出完全相同的 Color
    constexpr Color byInteger = Color::Hex(0xF7F7F5);
    constexpr Color byString = Color::FromHex("#F7F7F5");
    constexpr Color byBytes = Color::FromBytes(0xF7, 0xF7, 0xF5);
    EXPECT_EQ(byInteger, byBytes);
    EXPECT_EQ(byString, byBytes);
}

TEST(ColorBytes, QuantizesWithRounding) {
    EXPECT_EQ(Color::Rgb(0.0, 0.0, 0.0).toBytes(), (Rgba8{0, 0, 0, 255}));
    EXPECT_EQ(Color::Rgb(1.0, 1.0, 1.0).toBytes(), (Rgba8{255, 255, 255, 255}));
    EXPECT_EQ(Color::Rgb(0.5, 0.5, 0.5).toBytes(), (Rgba8{128, 128, 128, 255}));
}

TEST(ColorBytes, ClampsOutOfRange) {
    EXPECT_EQ(Color::Rgba(-1.0, 2.0, 0.5, 0.5).toBytes(), (Rgba8{0, 255, 128, 128}));
}

TEST(ColorValue, WithAlphaReturnsCopy) {
    const Color base = Color::Hex(0x3E6DB5);
    const Color dim = base.withAlpha(0.25);
    EXPECT_FLOAT_EQ(dim.a, 0.25f);
    EXPECT_FLOAT_EQ(base.a, 1.0f);  // 原值不被改，调色板常量才安全
}

TEST(ColorValue, WithOpacityMultipliesExistingAlpha) {
    const Color half = Color::Hex(0x3E6DB580u);  // 0x80 => 128/255
    const Color quarter = half.withOpacity(0.5);
    EXPECT_NEAR(quarter.a, (128.0 / 255.0) * 0.5, 1e-5);
}

TEST(ColorValue, MixInterpolatesAllChannels) {
    const Color a = Color::Rgba(0.0, 0.0, 0.0, 0.0);
    const Color b = Color::Rgba(1.0, 1.0, 1.0, 1.0);
    const Color mid = Color::mix(a, b, 0.5);
    EXPECT_FLOAT_EQ(mid.r, 0.5f);
    EXPECT_FLOAT_EQ(mid.a, 0.5f);
    EXPECT_EQ(Color::mix(a, b, -3.0), a);  // t 夹紧
    EXPECT_EQ(Color::mix(a, b, 3.0), b);
}

}  // namespace
}  // namespace kiriyama::core
