#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "kiriyama/render/Font.h"
#include "kiriyama/render/Fonts.generated.h"
#include "kiriyama/render/TextLayout.h"
#include "kiriyama/render/TtfFont.h"

namespace {

using kiriyama::render::Font;
using kiriyama::render::FontLibrary;
using kiriyama::render::TextLayout;
using kiriyama::render::TextLayoutOptions;
using kiriyama::render::TtfFontFace;
namespace embedded = kiriyama::render::embedded;

// 用哪支字体不写死：跟随 -DKIRIYAMA_FONTS，默认 JetBrainsMono-Light。
// 这样"测过的字体"就是"打进产物的字体"。
const std::string kBodyFont{embedded::kBodyFont};
const std::string kIconFont{embedded::kIconFont};

class TtfFontTest : public ::testing::Test {
protected:
    void SetUp() override { face_ = TtfFontFace::open(kBodyFont); }

    std::shared_ptr<const TtfFontFace> face_;
};

TEST_F(TtfFontTest, LoadsFontAndReportsFamily) {
    ASSERT_NE(face_, nullptr);
    EXPECT_FALSE(face_->family().empty());
    EXPECT_TRUE(face_->valid());
    EXPECT_FALSE(face_->path().empty());
}

TEST_F(TtfFontTest, MissingFileReturnsNull) {
    EXPECT_EQ(TtfFontFace::open("public/does-not-exist.ttf"), nullptr);
}

TEST_F(TtfFontTest, MetricsScaleWithPixelSize) {
    ASSERT_NE(face_, nullptr);

    const auto small = face_->metrics(12.0);
    const auto large = face_->metrics(48.0);

    EXPECT_GT(small.ascent, 0.0);
    EXPECT_GT(small.descent, 0.0);
    EXPECT_GE(small.lineGap, 0.0);
    EXPECT_NEAR(small.lineHeight(), small.ascent + small.descent + small.lineGap, 1e-9);

    // 4 倍字号应当给出约 4 倍的行高（TrueType 是线性的，允许 1px 取整误差）
    EXPECT_NEAR(large.lineHeight(), small.lineHeight() * 4.0, 4.0);
}

TEST_F(TtfFontTest, ProportionalAdvances) {
    ASSERT_NE(face_, nullptr);

    // 默认字体是等宽的 JetBrains Mono，所以这里只验证"推进量随字号线性增长"，
    // 不断言 M 比 i 宽 —— 换成 Roboto 这类比例字体时那条断言毫无意义。
    const double small = face_->advance(U'M', 16.0);
    const double large = face_->advance(U'M', 32.0);
    const double space = face_->advance(U' ', 16.0);

    EXPECT_GT(small, 0.0);
    EXPECT_GT(space, 0.0);
    // advance 是按整像素返回的（10px@16 / 19px@32 这种），所以留 1.5px 容差，
    // 只验证"随字号线性增长"，不跟量化误差较劲。
    EXPECT_NEAR(large, small * 2.0, 1.5);
}

TEST_F(TtfFontTest, GlyphCoverage) {
    ASSERT_NE(face_, nullptr);

    EXPECT_TRUE(face_->supports(U'A'));
    EXPECT_TRUE(face_->supports(U'0'));
    EXPECT_TRUE(face_->supports(U'%'));

    // 保留区码点任何正文/图标字体都不该有，supports 必须如实返回 false
    // （渲染层据此画豆腐块，而不是画一个错误的字形）。
    EXPECT_FALSE(face_->supports(0x10FFFD));
}

TEST_F(TtfFontTest, IconFontHasPrivateUseGlyphs) {
    if (kIconFont.empty()) {
        GTEST_SKIP() << "本次构建没有打包图标字体（KIRIYAMA_FONTS 里没有 fontawesome-webfont）";
    }

    const auto icons = TtfFontFace::open(kIconFont);
    ASSERT_NE(icons, nullptr);

    EXPECT_TRUE(icons->supports(0xF080));  // fa-bar-chart
    EXPECT_FALSE(icons->supports(U'A'));
}

TEST_F(TtfFontTest, LayoutWidthMatchesAdvanceSum) {
    ASSERT_NE(face_, nullptr);
    const Font font(face_, 16.0);

    TextLayoutOptions options;
    const std::string text = "Revenue +12.4%";
    const TextLayout layout = kiriyama::render::layoutText(text, font, options);

    double expected = 0.0;
    for (const char32_t cp : text) {
        expected += font.advance(cp);
    }

    EXPECT_NEAR(layout.boxWidth, expected, 0.5);
    ASSERT_EQ(layout.lines.size(), 1u);
    EXPECT_EQ(layout.lines.front().text, text);
}

TEST_F(TtfFontTest, WrappingNeverExceedsMaxWidth) {
    ASSERT_NE(face_, nullptr);
    const Font font(face_, 14.0);

    TextLayoutOptions options;
    options.maxWidth = 120.0;
    options.wrap = true;

    const TextLayout layout = kiriyama::render::layoutText(
        "Greedy line breaking fills each line until the next word would overflow.", font, options);

    EXPECT_GT(layout.lines.size(), 1u);
    for (const auto& line : layout.lines) {
        EXPECT_LE(line.width, options.maxWidth + 0.5) << "line: " << line.text;
    }
}

TEST_F(TtfFontTest, EllipsisRespectsMaxLines) {
    ASSERT_NE(face_, nullptr);
    const Font font(face_, 14.0);

    TextLayoutOptions options;
    options.maxWidth = 120.0;
    options.wrap = true;
    options.maxLines = 2;
    options.ellipsis = "...";

    const TextLayout layout = kiriyama::render::layoutText(
        "When a label must stay inside a fixed box, clamping to two lines and adding an "
        "ellipsis keeps the layout stable.",
        font, options);

    ASSERT_EQ(layout.lines.size(), 2u);
    const std::string& last = layout.lines.back().text;
    EXPECT_EQ(last.substr(last.size() - 3), "...");
}

TEST_F(TtfFontTest, LibraryCachesByResolvedPath) {
    FontLibrary& library = FontLibrary::shared();
    library.clear();

    const auto first = library.load(kBodyFont);
    const auto second = library.load(kBodyFont);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(library.loadedCount(), 1u);

    library.clear();
}

}  // namespace
