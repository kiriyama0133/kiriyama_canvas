#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "kiriyama/core/Geometry.h"
#include "kiriyama/render/Font.h"
#include "kiriyama/render/TextLayout.h"
#include "kiriyama/render/Utf8.h"

namespace {

using kiriyama::core::RectD;
using kiriyama::core::TextAlign;
using kiriyama::core::TextBaseline;
using namespace kiriyama::render;

/// 测试用等宽字体：字号 s 下每字符宽 0.5s，行高 1.1s。
/// 数值都是整数倍，断言可以写得刚好不擦边。
class MonoFace final : public FontFace {
public:
    [[nodiscard]] std::string_view family() const noexcept override { return "MonoTest"; }

    [[nodiscard]] FontMetrics metrics(double pixelSize) const noexcept override {
        return FontMetrics{pixelSize, pixelSize * 0.8, pixelSize * 0.2, pixelSize * 0.1};
    }

    [[nodiscard]] double advance(char32_t codepoint, double pixelSize) const noexcept override {
        return codepoint < 32 ? 0.0 : pixelSize * 0.5;
    }

    [[nodiscard]] bool supports(char32_t) const noexcept override { return true; }
};

[[nodiscard]] Font makeFont(double size = 10.0) {
    static const std::shared_ptr<const FontFace> face = std::make_shared<MonoFace>();
    return Font{face, size};
}

}  // namespace

// ---------------------------------------------------------------- UTF-8

TEST(Utf8, DecodesAsciiAndMultibyte) {
    const auto ascii = decodeUtf8("abc", 0);
    EXPECT_EQ(ascii.value, U'a');
    EXPECT_EQ(ascii.bytes, 1u);

    // U+4E2D 中
    const auto han = decodeUtf8("\xE4\xB8\xAD", 0);
    EXPECT_EQ(han.value, 0x4E2Du);
    EXPECT_EQ(han.bytes, 3u);

    EXPECT_EQ(codepointCount("\xE4\xB8\xAD" "ab"), 3u);
}

TEST(Utf8, InvalidSequencesAdvanceOneByte) {
    const auto bomb = decodeUtf8("\xFF", 0);
    EXPECT_EQ(bomb.value, kReplacementCharacter);
    EXPECT_EQ(bomb.bytes, 1u);

    // 截断的三字节序列
    const auto truncated = decodeUtf8("\xE4\xB8", 0);
    EXPECT_EQ(truncated.value, kReplacementCharacter);
    EXPECT_EQ(truncated.bytes, 1u);

    // 代理区（不合法的 UTF-8）
    const auto surrogate = decodeUtf8("\xED\xA0\x80", 0);
    EXPECT_EQ(surrogate.value, kReplacementCharacter);
    EXPECT_EQ(surrogate.bytes, 1u);
}

TEST(Utf8, EncodeRoundTrip) {
    EXPECT_EQ(encodeUtf8(U'a'), "a");
    EXPECT_EQ(encodeUtf8(0x4E2Du), "\xE4\xB8\xAD");
    EXPECT_EQ(encodeUtf8(0x1F600u), "\xF0\x9F\x98\x80");
}

TEST(Utf8, ClassifiesWideCharacters) {
    EXPECT_TRUE(isWide(U'\u4E2D'));
    EXPECT_TRUE(isWide(U'\u3042'));  // あ
    EXPECT_FALSE(isWide(U'a'));
    EXPECT_TRUE(isSpace(U' '));
    EXPECT_FALSE(isSpace(U'a'));
}

// ---------------------------------------------------------------- Font

TEST(Font, BuiltinFaceIsMonospaceAndAsciiOnly) {
    Font font(Font::builtinFace(), 16.0);
    EXPECT_DOUBLE_EQ(font.advance(U'A'), 16.0);
    EXPECT_DOUBLE_EQ(font.advance(U' '), 16.0);
    EXPECT_DOUBLE_EQ(font.measure("abc"), 48.0);
    EXPECT_TRUE(font.face().supports(U'A'));
    EXPECT_FALSE(font.face().supports(U'\u4E2D'));

    const FontMetrics metrics = font.metrics();
    EXPECT_DOUBLE_EQ(metrics.ascent, 16.0);
    EXPECT_DOUBLE_EQ(metrics.descent, 0.0);
    EXPECT_DOUBLE_EQ(metrics.lineHeight(), 20.0);
}

TEST(Font, SizeIsClampedAndCopied) {
    Font font = makeFont(10.0);
    font.setSize(-4.0);
    EXPECT_GE(font.size(), 1.0);
    font.setSize(100000.0);
    EXPECT_LE(font.size(), 512.0);

    const Font bigger = font.withSize(20.0);
    EXPECT_DOUBLE_EQ(bigger.size(), 20.0);
    EXPECT_DOUBLE_EQ(font.size(), 512.0);  // 原对象不受影响
}

// ---------------------------------------------------------------- 单行 / 换行

TEST(TextLayout, SingleLineMetrics) {
    const Font font = makeFont(10.0);
    const TextLayout layout = layoutText("abcd", font);

    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_DOUBLE_EQ(layout.lines[0].width, 20.0);
    EXPECT_DOUBLE_EQ(layout.width, 20.0);
    EXPECT_DOUBLE_EQ(layout.boxWidth, 20.0);  // 没有 maxWidth 时退化为墨迹宽度
    EXPECT_DOUBLE_EQ(layout.lineHeight, 11.0);
    EXPECT_DOUBLE_EQ(layout.height, 11.0);
    // half-leading = (11 - (8 + 2)) / 2 = 0.5
    EXPECT_DOUBLE_EQ(layout.lines[0].baseline, 8.5);
    EXPECT_FALSE(layout.truncated);
}

TEST(TextLayout, EmptyTextProducesEmptyLayout) {
    const TextLayout layout = layoutText("", makeFont());
    EXPECT_TRUE(layout.empty());
    EXPECT_DOUBLE_EQ(layout.height, 0.0);
}

TEST(TextLayout, HardLineBreaks) {
    const TextLayout layout = layoutText("a\nb\nc", makeFont());
    ASSERT_EQ(layout.lines.size(), 3u);
    EXPECT_EQ(layout.lines[0].text, "a");
    EXPECT_EQ(layout.lines[2].text, "c");
    EXPECT_DOUBLE_EQ(layout.height, 33.0);
    EXPECT_DOUBLE_EQ(layout.lines[1].top, 11.0);
}

TEST(TextLayout, CarriageReturnIsTrimmed) {
    const TextLayout layout = layoutText("ab\r\ncd", makeFont());
    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_EQ(layout.lines[0].text, "ab");
    EXPECT_DOUBLE_EQ(layout.lines[0].width, 10.0);
}

TEST(TextLayout, WrapsAtSpaces) {
    TextLayoutOptions options;
    options.maxWidth = 30.0;
    const TextLayout layout = layoutText("aa bb cc", makeFont(), options);

    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_EQ(layout.lines[0].text, "aa bb");   // 行尾空格被去掉
    EXPECT_DOUBLE_EQ(layout.lines[0].width, 25.0);
    EXPECT_EQ(layout.lines[1].text, "cc");
    EXPECT_LE(layout.width, 30.0);
}

TEST(TextLayout, BreaksBetweenWideCharacters) {
    TextLayoutOptions options;
    options.maxWidth = 15.0;
    // 四个 CJK 字符，每字 5px
    const TextLayout layout = layoutText("\u4E00\u4E8C\u4E09\u56DB", makeFont(), options);

    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_EQ(codepointCount(layout.lines[0].text), 3u);
    EXPECT_EQ(codepointCount(layout.lines[1].text), 1u);
    EXPECT_DOUBLE_EQ(layout.lines[1].width, 5.0);
}

TEST(TextLayout, HardBreaksLongWord) {
    TextLayoutOptions options;
    options.maxWidth = 25.0;
    const TextLayout layout = layoutText("abcdefgh", makeFont(), options);

    ASSERT_EQ(layout.lines.size(), 2u);
    EXPECT_EQ(layout.lines[0].text, "abcde");
    EXPECT_EQ(layout.lines[1].text, "fgh");
}

TEST(TextLayout, NoWrapKeepsSingleLine) {
    TextLayoutOptions options;
    options.maxWidth = 10.0;
    options.wrap = false;
    const TextLayout layout = layoutText("abcdef", makeFont(), options);

    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_DOUBLE_EQ(layout.lines[0].width, 30.0);  // 允许溢出
    EXPECT_DOUBLE_EQ(layout.boxWidth, 10.0);
}

// ---------------------------------------------------------------- 截断

TEST(TextLayout, TruncatesWithEllipsis) {
    TextLayoutOptions options;
    options.maxWidth = 25.0;
    options.maxLines = 1;
    options.ellipsis = "...";
    const TextLayout layout = layoutText("abcdefgh", makeFont(), options);

    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_TRUE(layout.truncated);
    EXPECT_EQ(layout.lines[0].text, "ab...");
    EXPECT_LE(layout.lines[0].width, 25.0);
}

TEST(TextLayout, NoTruncationWhenItFits) {
    TextLayoutOptions options;
    options.maxWidth = 100.0;
    options.maxLines = 2;
    const TextLayout layout = layoutText("short", makeFont(), options);

    EXPECT_FALSE(layout.truncated);
    EXPECT_EQ(layout.lines[0].text, "short");
}

// ---------------------------------------------------------------- 对齐 / 字距

TEST(TextLayout, AlignsWithinBox) {
    const Font font = makeFont();
    TextLayoutOptions options;
    options.maxWidth = 100.0;

    options.align = TextAlign::Left;
    EXPECT_DOUBLE_EQ(layoutText("abcd", font, options).lines[0].x, 0.0);

    options.align = TextAlign::Center;
    EXPECT_DOUBLE_EQ(layoutText("abcd", font, options).lines[0].x, 40.0);

    options.align = TextAlign::Right;
    EXPECT_DOUBLE_EQ(layoutText("abcd", font, options).lines[0].x, 80.0);
}

TEST(TextLayout, AlignsWithinInkWidthWithoutMaxWidth) {
    TextLayoutOptions options;
    options.align = TextAlign::Right;
    const TextLayout layout = layoutText("abcd", makeFont(), options);
    EXPECT_DOUBLE_EQ(layout.boxWidth, 20.0);
    EXPECT_DOUBLE_EQ(layout.lines[0].x, 0.0);  // 只有一行时右对齐等于左对齐
}

TEST(TextLayout, LetterSpacingAddsBetweenCharactersOnly) {
    TextLayoutOptions options;
    options.letterSpacing = 2.0;
    const TextLayout layout = layoutText("abcd", makeFont(), options);
    EXPECT_DOUBLE_EQ(layout.lines[0].width, 26.0);  // 4 * 5 + 3 * 2
}

TEST(TextLayout, TabUsesConfiguredWidth) {
    TextLayoutOptions options;
    options.tabWidth = 20.0;
    const TextLayout layout = layoutText("a\tb", makeFont(), options);
    EXPECT_DOUBLE_EQ(layout.lines[0].width, 30.0);
}

// ---------------------------------------------------------------- 摆放

TEST(TextLayout, PlacesTextBoxByBaselineMode) {
    const TextLayout layout = layoutText("a\nb\nc", makeFont());  // height = 33
    const RectD frame{10.0, 100.0, 200.0, 40.0};

    EXPECT_EQ(placeTextBox(frame, layout, TextBaseline::Top), (Vec2d{10.0, 100.0}));
    EXPECT_DOUBLE_EQ(placeTextBox(frame, layout, TextBaseline::Middle).y, 103.5);
    EXPECT_DOUBLE_EQ(placeTextBox(frame, layout, TextBaseline::Bottom).y, 107.0);
}
