#pragma once

// 语义 Token 调色板：把"用哪个语义色"和"语义色是什么"拆开。
//
// 描述树（core）不再烘焙任何颜色——节点上只写语义 Token（背景 / 网格 / 标签 /
// 第 i 个系列……），渲染层拿当前 ChartTheme 解析成真实颜色。收益：
//   · 深色 / 浅色切换 = 换一个 ChartTheme，**场景树原封不动**，不需要 rebuild
//     （布局只依赖字号等度量，而度量已经不在 theme 里了）；
//   · 主题成为页面级状态（C API kiriyama_set_theme），所有实例统一生效；
//   · 组件还能沿树覆盖局部系列色板（SceneStyle::seriesPalette），
//     实例自定义色板与全局主题互不干扰。
//
// Paint 是 SceneStyle 里 fill / stroke 的类型：
//   token == None  → 用直接色 color（表格样式、组件自定义色等）；
//   token != None  → resolve(theme) 时查调色板；alphaScale 做透明度缩放
//                    （面积填充、柱子的淡色都靠它，不破坏 token 语义）。

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Color.h"

namespace kiriyama::core {

/// 语义色。None 之外的颜色都到 ChartTheme 里查。
enum class ColorToken : std::uint8_t {
    None,
    Background,      ///< 画布背景
    PlotBackground,  ///< 绘图区背景
    Grid,            ///< 网格线
    AxisLine,        ///< 轴线
    Label,           ///< 轴刻度 / 数值标签等正文文本
    Title,           ///< 图表标题
    EmptyText,       ///< 空数据提示
    Series,          ///< 系列色（配 seriesIndex，超出色板循环取用）
};

struct ChartTheme;

/// 一次填充 / 描边 / 文字上色的描述：语义 token 或直接色。
struct Paint {
    ColorToken token{ColorToken::None};
    std::size_t seriesIndex{0};  ///< 仅 token == Series 时生效
    Color color{};               ///< 仅 token == None 时生效
    float alphaScale{1.0f};      ///< 透明度缩放（面积 / 柱子的淡色）

    constexpr Paint() noexcept = default;

    /// 允许 `style.fill = Color::...` 的直接色写法（存量调用零改动）。
    constexpr Paint(Color c) noexcept : color(c) {}
    constexpr Paint(ColorToken t) noexcept : token(t) {}

    /// 第 index 个系列色，可选透明度缩放。
    [[nodiscard]] static constexpr Paint series(std::size_t index,
                                                float alphaScale = 1.0f) noexcept {
        Paint paint;
        paint.token = ColorToken::Series;
        paint.seriesIndex = index;
        paint.alphaScale = alphaScale;
        return paint;
    }

    [[nodiscard]] constexpr bool visible() const noexcept {
        return token != ColorToken::None || color.visible();
    }

    /// 解析成真实颜色。seriesOverride 非空时替换全局系列色板
    ///（SceneStyle::seriesPalette 沿树继承下来的实例色板）。
    [[nodiscard]] Color resolve(const ChartTheme& theme,
                                const std::vector<Color>* seriesOverride = nullptr) const;

    [[nodiscard]] constexpr bool operator==(const Paint&) const noexcept = default;
};

/// 调色板：只有颜色。字号 / 线宽等度量在 SceneBuildOptions（布局参数）里，
/// 这样深浅主题只要颜色不同，切换主题就完全不需要重建场景树。
struct ChartTheme {
    Color background{Color::FromBytes(0xFF, 0xFF, 0xFF)};
    Color plotBackground{Color::FromBytes(0xFA, 0xFA, 0xF8)};
    Color grid{Color::FromBytes(0xE9, 0xE7, 0xE1)};
    Color axisLine{Color::FromBytes(0xB4, 0xB2, 0xAA)};
    Color label{Color::FromBytes(0x6E, 0x6E, 0x68)};
    Color title{Color::FromBytes(0x2C, 0x2C, 0x2A)};
    Color emptyText{Color::FromBytes(0xA8, 0xA6, 0x9E)};

    /// 为空时用 defaultSeriesPalette()。
    std::vector<Color> series{};

    [[nodiscard]] static ChartTheme Light();
    [[nodiscard]] static ChartTheme Dark();

    /// 超过色板长度自动循环。
    [[nodiscard]] Color seriesColor(std::size_t index) const;
};

/// 8 色分类色板（蓝 / 青 / 橙 / 紫 / 红 / 绿 / 灰蓝 / 玫红）。
[[nodiscard]] const std::vector<Color>& defaultSeriesPalette();

}  // namespace kiriyama::core
