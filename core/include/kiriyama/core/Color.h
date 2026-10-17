#pragma once

// 颜色：线性 RGBA，分量 0~1。
//
// 用 float 而不是 uint8：渲染层做透明度叠加、渐变插值时不会在中途丢精度，
// 需要 8 位色值的地方统一在输出前转（toBytes()）。
//
// 构造入口按用途挑：
//   Color::Hex(0xF7F7F5)          设计稿里直接抄整数
//   Color::FromHex("#F7F7F5AA")   配置 / JSON / CSS 来的字符串，可带 alpha
//   Color::FromBytes(247,247,245) 已经有 0~255 分量时

#include <cstdint>
#include <optional>
#include <string_view>

namespace kiriyama::core {

/// 8 位通道。渲染后端（SDL_Color / canvas ImageData）交换用的格式，
/// core 里只在输出边界出现，不参与计算。
struct Rgba8 {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{0};

    [[nodiscard]] constexpr bool operator==(const Rgba8&) const noexcept = default;
};

/// 线性 RGBA，分量 0~1。默认全透明（未设样式 = 不可见），
/// 这样"新建了节点但忘了设色"不会在画布上凭空多出一个黑块。
struct Color {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
    float a{0.0f};

    [[nodiscard]] static constexpr Color Rgb(double red, double green, double blue) noexcept {
        return Rgba(red, green, blue, 1.0);
    }
    [[nodiscard]] static constexpr Color Rgba(double red, double green, double blue,
                                              double alpha) noexcept {
        return {static_cast<float>(red), static_cast<float>(green), static_cast<float>(blue),
                static_cast<float>(alpha)};
    }
    /// 0~255 的整数入口，方便直接抄设计稿色值。
    [[nodiscard]] static constexpr Color FromBytes(int red, int green, int blue,
                                                   int alpha = 255) noexcept {
        return Rgba(static_cast<double>(red) / 255.0, static_cast<double>(green) / 255.0,
                    static_cast<double>(blue) / 255.0, static_cast<double>(alpha) / 255.0);
    }

    /// 0xRRGGBB，超过 0xFFFFFF 时按 0xRRGGBBAA 解释（省得为带 alpha 换个函数名）。
    [[nodiscard]] static constexpr Color Hex(std::uint32_t value) noexcept {
        if (value > 0xFFFFFFu) {
            return FromBytes(static_cast<int>((value >> 24) & 0xFFu),
                             static_cast<int>((value >> 16) & 0xFFu),
                             static_cast<int>((value >> 8) & 0xFFu),
                             static_cast<int>(value & 0xFFu));
        }
        return FromBytes(static_cast<int>((value >> 16) & 0xFFu),
                         static_cast<int>((value >> 8) & 0xFFu),
                         static_cast<int>(value & 0xFFu));
    }

    /// 字符串入口，接受 "#RGB" / "#RGBA" / "#RRGGBB" / "#RRGGBBAA"，'#' 可省。
    /// 非法输入返回全透明（不可见），需要区分时用 TryParseHex。
    [[nodiscard]] static constexpr Color FromHex(std::string_view text) noexcept {
        return TryParseHex(text).value_or(Color{});
    }

    /// 同 FromHex，但会把"解析失败"明确告诉调用方——配置里读色值时用它。
    [[nodiscard]] static constexpr std::optional<Color> TryParseHex(
        std::string_view text) noexcept {
        if (!text.empty() && text.front() == '#') {
            text.remove_prefix(1);
        }
        for (const char c : text) {
            if (hexDigit(c) < 0) {
                return std::nullopt;
            }
        }
        switch (text.size()) {
            case 3:  // #RGB：每位重复一次，f == ff
                return FromBytes(hexDigit(text[0]) * 17, hexDigit(text[1]) * 17,
                                 hexDigit(text[2]) * 17);
            case 4:  // #RGBA
                return FromBytes(hexDigit(text[0]) * 17, hexDigit(text[1]) * 17,
                                 hexDigit(text[2]) * 17, hexDigit(text[3]) * 17);
            case 6:  // #RRGGBB
                return FromBytes(hexDigit(text[0]) * 16 + hexDigit(text[1]),
                                 hexDigit(text[2]) * 16 + hexDigit(text[3]),
                                 hexDigit(text[4]) * 16 + hexDigit(text[5]));
            case 8:  // #RRGGBBAA
                return FromBytes(hexDigit(text[0]) * 16 + hexDigit(text[1]),
                                 hexDigit(text[2]) * 16 + hexDigit(text[3]),
                                 hexDigit(text[4]) * 16 + hexDigit(text[5]),
                                 hexDigit(text[6]) * 16 + hexDigit(text[7]));
            default:
                return std::nullopt;
        }
    }

    [[nodiscard]] constexpr bool visible() const noexcept { return a > 0.0f; }

    /// 换个 alpha，返回副本——颜色本身是不可变值，改一个分量不该改到调色板常量上。
    [[nodiscard]] constexpr Color withAlpha(double alpha) const noexcept {
        return Rgba(r, g, b, alpha);
    }

    /// 在现有 alpha 上再乘一个系数（0~1），用于"整体压暗 / 淡化"。
    [[nodiscard]] constexpr Color withOpacity(double factor) const noexcept {
        return Rgba(r, g, b, static_cast<double>(a) * factor);
    }

    /// 线性插值，渐变 / hover 过渡用。t 会被夹到 0~1。
    [[nodiscard]] static constexpr Color mix(const Color& from, const Color& to,
                                             double t) noexcept {
        const double k = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return Rgba(from.r + (to.r - from.r) * k, from.g + (to.g - from.g) * k,
                    from.b + (to.b - from.b) * k, from.a + (to.a - from.a) * k);
    }

    /// 输出到 8 位。所有渲染后端都从这里取数，不要各自写一遍四舍五入。
    [[nodiscard]] constexpr Rgba8 toBytes() const noexcept {
        return Rgba8{quantize(r), quantize(g), quantize(b), quantize(a)};
    }

    [[nodiscard]] constexpr bool operator==(const Color&) const noexcept = default;

private:
    [[nodiscard]] static constexpr int hexDigit(char c) noexcept {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    [[nodiscard]] static constexpr std::uint8_t quantize(float value) noexcept {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
    }
};

}  // namespace kiriyama::core
