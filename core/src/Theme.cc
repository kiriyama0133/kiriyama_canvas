#include "kiriyama/core/Theme.h"

namespace kiriyama::core {

Color Paint::resolve(const ChartTheme& theme, const std::vector<Color>* seriesOverride) const {
    Color out = color;
    switch (token) {
        case ColorToken::None:
            break;
        case ColorToken::Background:
            out = theme.background;
            break;
        case ColorToken::PlotBackground:
            out = theme.plotBackground;
            break;
        case ColorToken::Grid:
            out = theme.grid;
            break;
        case ColorToken::AxisLine:
            out = theme.axisLine;
            break;
        case ColorToken::Label:
            out = theme.label;
            break;
        case ColorToken::Title:
            out = theme.title;
            break;
        case ColorToken::EmptyText:
            out = theme.emptyText;
            break;
        case ColorToken::Series:
            out = seriesOverride != nullptr && !seriesOverride->empty()
                      ? (*seriesOverride)[seriesIndex % seriesOverride->size()]
                      : theme.seriesColor(seriesIndex);
            break;
    }
    if (alphaScale != 1.0f && out.a != 0) {
        out.a = static_cast<std::uint8_t>(static_cast<float>(out.a) * alphaScale + 0.5f);
    }
    return out;
}

const std::vector<Color>& defaultSeriesPalette() {
    static const std::vector<Color> palette = {
        Color::FromBytes(0x3E, 0x6D, 0xB5),  // 蓝
        Color::FromBytes(0x2E, 0x8A, 0x85),  // 青
        Color::FromBytes(0xC2, 0x76, 0x2A),  // 橙
        Color::FromBytes(0x7A, 0x5A, 0xA6),  // 紫
        Color::FromBytes(0xC0, 0x50, 0x4D),  // 红
        Color::FromBytes(0x4F, 0x8A, 0x3D),  // 绿
        Color::FromBytes(0x6B, 0x72, 0x80),  // 灰蓝
        Color::FromBytes(0xB4, 0x5C, 0x7F),  // 玫红
    };
    return palette;
}

ChartTheme ChartTheme::Light() {
    ChartTheme theme;
    theme.series = defaultSeriesPalette();
    return theme;
}

ChartTheme ChartTheme::Dark() {
    ChartTheme theme;
    theme.background = Color::FromBytes(0x1C, 0x1C, 0x1E);
    theme.plotBackground = Color::FromBytes(0x24, 0x24, 0x27);
    theme.grid = Color::FromBytes(0x3A, 0x3A, 0x3F);
    theme.axisLine = Color::FromBytes(0x55, 0x55, 0x5C);
    theme.label = Color::FromBytes(0xA8, 0xA8, 0xB0);
    theme.title = Color::FromBytes(0xEC, 0xEC, 0xF0);
    theme.emptyText = Color::FromBytes(0x77, 0x77, 0x80);
    theme.series = {
        Color::FromBytes(0x6F, 0xA8, 0xDC),
        Color::FromBytes(0x4F, 0xB3, 0xAC),
        Color::FromBytes(0xE0, 0xA4, 0x58),
        Color::FromBytes(0xA9, 0x8B, 0xD6),
        Color::FromBytes(0xE0, 0x7A, 0x75),
        Color::FromBytes(0x7D, 0xB8, 0x6A),
        Color::FromBytes(0x96, 0xA0, 0xAE),
        Color::FromBytes(0xD9, 0x8B, 0xAB),
    };
    return theme;
}

Color ChartTheme::seriesColor(std::size_t index) const {
    if (!series.empty()) {
        return series[index % series.size()];
    }
    return defaultSeriesPalette()[index % defaultSeriesPalette().size()];
}

}  // namespace kiriyama::core
