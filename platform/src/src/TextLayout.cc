#include "kiriyama/render/TextLayout.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "kiriyama/render/Utf8.h"

namespace kiriyama::render {
namespace {

/// 一次排版最多产出多少行。防止异常输入（比如几十兆没有空格的串）把内存吃穿。
constexpr std::size_t kMaxLines = 4096;

struct RawLine {
    std::string text;
    double width{0.0};
};

/// 宽度计算 + 去空白的小工具，保证"断行时算的宽度"和"渲染时算的宽度"同源。
class LineBuilder {
public:
    LineBuilder(const Font& font, double tabWidth, double letterSpacing)
        : font_(&font), tabWidth_(tabWidth), letterSpacing_(letterSpacing) {}

    [[nodiscard]] double letterSpacing() const noexcept { return letterSpacing_; }

    [[nodiscard]] double advanceOf(char32_t codepoint) const {
        return codepoint == U'\t' ? tabWidth_ : font_->advance(codepoint);
    }

    /// 一行的墨迹宽度：字符前进宽度之和 + 字距（字距不落在首字符之前）。
    [[nodiscard]] double measure(std::string_view line) const {
        double width = 0.0;
        bool first = true;
        std::size_t offset = 0;
        while (offset < line.size()) {
            const DecodedCodepoint decoded = decodeUtf8(line, offset);
            offset += decoded.bytes == 0 ? 1 : decoded.bytes;
            if (!first) {
                width += letterSpacing_;
            }
            width += advanceOf(decoded.value);
            first = false;
        }
        return width;
    }

    /// 去掉行尾空白（含 \r）。
    [[nodiscard]] static std::string trimRight(std::string_view line) {
        std::size_t end = line.size();
        while (end > 0) {
            std::size_t start = end - 1;
            while (start > 0 && (static_cast<unsigned char>(line[start]) & 0xC0) == 0x80) {
                --start;
            }
            const DecodedCodepoint decoded = decodeUtf8(line, start);
            if (decoded.bytes == 0 || (!isSpace(decoded.value) && decoded.value != U'\r')) {
                break;
            }
            end = start;
        }
        return std::string(line.substr(0, end));
    }

private:
    const Font* font_;
    double tabWidth_;
    double letterSpacing_;
};

/// 段落内的一个码点。
struct Item {
    char32_t value{0};
    std::size_t byteOffset{0};  ///< 在段落里的起始字节
    std::size_t bytes{1};
    double advance{0.0};
    bool space{false};
    bool wide{false};
};

[[nodiscard]] std::vector<Item> splitItems(std::string_view paragraph, const LineBuilder& builder) {
    std::vector<Item> items;
    items.reserve(paragraph.size());
    std::size_t offset = 0;
    while (offset < paragraph.size()) {
        const DecodedCodepoint decoded = decodeUtf8(paragraph, offset);
        const std::size_t bytes = decoded.bytes == 0 ? 1 : decoded.bytes;
        items.push_back(Item{decoded.value, offset, bytes, builder.advanceOf(decoded.value),
                             isSpace(decoded.value), isWide(decoded.value)});
        offset += bytes;
    }
    return items;
}

/// 贪心断行，段落内部不含 '\n'。
void wrapParagraph(std::string_view paragraph, const LineBuilder& builder, double maxWidth,
                   bool wrap, std::vector<RawLine>& lines) {
    if (!wrap || maxWidth <= 0.0) {
        std::string text = LineBuilder::trimRight(paragraph);
        lines.push_back({text, builder.measure(text)});
        return;
    }

    const std::vector<Item> items = splitItems(paragraph, builder);
    if (items.empty()) {
        lines.push_back({std::string{}, 0.0});
        return;
    }

    const std::size_t count = items.size();
    const double letterSpacing = builder.letterSpacing();
    std::size_t start = 0;

    while (start < count) {
        double width = 0.0;
        std::size_t cursor = start;
        std::size_t lastBreak = start;
        bool hasBreak = false;

        while (cursor < count) {
            const double adv = items[cursor].advance + (cursor > start ? letterSpacing : 0.0);
            if (cursor > start && width + adv > maxWidth) {
                break;
            }
            width += adv;
            ++cursor;

            if (items[cursor - 1].space) {
                lastBreak = cursor;  // 空格之后可以断
                hasBreak = true;
            } else if (cursor < count && (items[cursor - 1].wide || items[cursor].wide)) {
                lastBreak = cursor;  // 宽字符之间可以断（中文按字断行）
                hasBreak = true;
            }
        }

        std::size_t end = cursor;
        if (end < count && hasBreak && lastBreak > start) {
            end = lastBreak;
        }
        if (end == start) {
            end = start + 1;  // 单个码点就超宽：至少放一个，避免死循环
        }

        const std::size_t byteBegin = items[start].byteOffset;
        const std::size_t byteEnd = items[end - 1].byteOffset + items[end - 1].bytes;
        std::string text = LineBuilder::trimRight(paragraph.substr(byteBegin, byteEnd - byteBegin));

        if (text.empty() && end < count) {
            // 整行都是空白且后面还有内容：不产生空行
            start = end;
            continue;
        }
        lines.push_back({text, builder.measure(text)});
        start = end;
    }
}

}  // namespace

TextLayout layoutText(std::string_view text, const Font& font,
                      const TextLayoutOptions& options) {
    TextLayout out;
    out.metrics = font.metrics();
    out.lineHeight = options.lineHeight > 0.0 ? options.lineHeight : out.metrics.lineHeight();
    if (!(out.lineHeight > 0.0)) {
        out.lineHeight = font.size() * 1.25;
    }
    if (text.empty()) {
        return out;
    }

    const double tabWidth = options.tabWidth > 0.0 ? options.tabWidth : font.advance(U' ') * 4.0;
    const LineBuilder builder(font, tabWidth, options.letterSpacing);
    const double maxWidth = options.maxWidth > 0.0 ? options.maxWidth : 0.0;
    const bool wrap = options.wrap && maxWidth > 0.0;

    std::vector<RawLine> raw;
    raw.reserve(8);

    std::size_t begin = 0;
    while (true) {
        const std::size_t newline = text.find('\n', begin);
        const std::string_view paragraph =
            newline == std::string_view::npos ? text.substr(begin) : text.substr(begin, newline - begin);
        wrapParagraph(paragraph, builder, maxWidth, wrap, raw);

        if (newline == std::string_view::npos) {
            break;
        }
        begin = newline + 1;
        if (raw.size() >= kMaxLines) {
            out.truncated = true;
            break;
        }
    }

    if (raw.empty()) {
        raw.push_back({std::string{}, 0.0});
    }

    // ---- 行数上限
    if (options.maxLines > 0 && raw.size() > static_cast<std::size_t>(options.maxLines)) {
        out.truncated = true;
        raw.resize(static_cast<std::size_t>(options.maxLines));
    }

    // ---- 省略号：把最后一行削到放得下省略号为止
    if (out.truncated && !options.ellipsis.empty()) {
        RawLine& last = raw.back();
        if (maxWidth <= 0.0) {
            last.text += options.ellipsis;
            last.width = builder.measure(last.text);
        } else {
            const double ellipsisWidth = builder.measure(options.ellipsis);
            while (!last.text.empty() && builder.measure(last.text) + ellipsisWidth > maxWidth) {
                std::size_t end = last.text.size();
                std::size_t start = end - 1;
                while (start > 0 && (static_cast<unsigned char>(last.text[start]) & 0xC0) == 0x80) {
                    --start;
                }
                last.text.erase(start);
            }
            last.text += options.ellipsis;
            last.width = builder.measure(last.text);
        }
    }

    // ---- 水平对齐 + 垂直位置
    double inkWidth = 0.0;
    for (const RawLine& line : raw) {
        inkWidth = std::max(inkWidth, line.width);
    }
    const double boxWidth = maxWidth > 0.0 ? maxWidth : inkWidth;
    const double halfLeading =
        std::max(0.0, (out.lineHeight - (out.metrics.ascent + out.metrics.descent)) * 0.5);

    out.lines.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        TextLine line;
        line.text = std::move(raw[i].text);
        line.width = raw[i].width;
        switch (options.align) {
            case TextAlign::Center:
                line.x = (boxWidth - line.width) * 0.5;
                break;
            case TextAlign::Right:
                line.x = boxWidth - line.width;
                break;
            case TextAlign::Left:
            default:
                line.x = 0.0;
                break;
        }
        line.top = static_cast<double>(i) * out.lineHeight;
        line.baseline = line.top + halfLeading + out.metrics.ascent;
        out.lines.push_back(std::move(line));
    }

    out.width = inkWidth;
    out.boxWidth = boxWidth;
    out.height = static_cast<double>(out.lines.size()) * out.lineHeight;
    return out;
}

Vec2d placeTextBox(const core::RectD& frame, const TextLayout& layout,
                   TextBaseline baseline) noexcept {
    double top = frame.top();
    switch (baseline) {
        case TextBaseline::Middle:
            top = frame.top() + (frame.height - layout.height) * 0.5;
            break;
        case TextBaseline::Bottom:
            top = frame.bottom() - layout.height;
            break;
        case TextBaseline::Top:
        default:
            break;
    }
    return {frame.left(), top};
}

}  // namespace kiriyama::render
