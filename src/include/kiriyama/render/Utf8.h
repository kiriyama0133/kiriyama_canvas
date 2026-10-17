#pragma once

// UTF-8 编解码与字符分类。
//
// 排版层只关心"码点 + 字节长度"这一对信息，所以这里不做任何 locale / 大小写
// 处理，也不建全局表。所有函数都是无状态的纯函数，方便在 wasm 里内联。

#include <cstddef>
#include <string>
#include <string_view>

namespace kiriyama::render {

inline constexpr char32_t kReplacementCharacter = 0xFFFD;

struct DecodedCodepoint {
    char32_t value{0};
    std::size_t bytes{0};  ///< 0 表示越界（文本结束）
};

/// 从 offset 解码一个码点。非法序列返回 U+FFFD 且只前进 1 字节，
/// 保证遍历永远不会卡住。
[[nodiscard]] inline DecodedCodepoint decodeUtf8(std::string_view text,
                                                 std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return {};
    }
    const auto lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80) {
        return {static_cast<char32_t>(lead), 1};
    }

    int length = 0;
    char32_t value = 0;
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1Fu;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0Fu;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07u;
    } else {
        return {kReplacementCharacter, 1};
    }

    if (offset + static_cast<std::size_t>(length) > text.size()) {
        return {kReplacementCharacter, 1};
    }
    for (int i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[offset + static_cast<std::size_t>(i)]);
        if ((byte & 0xC0) != 0x80) {
            return {kReplacementCharacter, 1};
        }
        value = (value << 6) | (byte & 0x3Fu);
    }

    // 过长编码、代理区、越界码点一律当非法输入
    const bool overlong = (length == 2 && value < 0x80) || (length == 3 && value < 0x800) ||
                          (length == 4 && value < 0x10000);
    const bool surrogate = value >= 0xD800 && value <= 0xDFFF;
    if (overlong || surrogate || value > 0x10FFFF) {
        return {kReplacementCharacter, 1};
    }
    return {value, static_cast<std::size_t>(length)};
}

[[nodiscard]] inline std::string encodeUtf8(char32_t codepoint) {
    std::string out;
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        return encodeUtf8(kReplacementCharacter);
    }
    return out;
}

[[nodiscard]] inline std::size_t codepointCount(std::string_view text) noexcept {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const DecodedCodepoint decoded = decodeUtf8(text, offset);
        offset += decoded.bytes == 0 ? 1 : decoded.bytes;
        ++count;
    }
    return count;
}

/// 断行时可丢弃的空白（不包含换行，换行由排版器单独处理）。
[[nodiscard]] inline constexpr bool isSpace(char32_t c) noexcept {
    return c == U' ' || c == U'\t' || c == U'\u00A0' || c == U'\u3000';
}

/// 宽字符（CJK / 假名 / 谚文 / 全角）：可以在任意两字之间断行。
[[nodiscard]] inline constexpr bool isWide(char32_t c) noexcept {
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0x303E) ||
           (c >= 0x3041 && c <= 0x33FF) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xA000 && c <= 0xA4CF) ||
           (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF60) ||
           (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) ||
           (c >= 0x20000 && c <= 0x2FA1F);
}

}  // namespace kiriyama::render
