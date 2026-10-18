#pragma once

// Tooltip 的 SDL 后端：量文字 + 画卡片 + 画箭头。
//
// 分工很硬：
//   core::Tooltip  —— 气泡摆哪、箭头指哪、什么时候显示（纯几何/纯时间，可单测）
//   本类            —— 只做两件 core 做不了的事：量文字、落笔
// 所以想换成 canvas / Skia 后端，只要照着 TooltipLayout 再写一个渲染器，
// 定位逻辑一行都不用重写。
//
// 用法（配合 core::TooltipState）：
//
//   core::TooltipState state;
//   TooltipRenderer tip(app.painter(), app.text());
//
//   // 每帧
//   state.hover(hoveredId, anchorRect, nowMs);
//   if (state.update(nowMs).visible) {
//       tip.draw(state.anchor(), {"Revenue", "2026 Q3  +12.4%"}, app.canvas(),
//                state.opacity(nowMs));
//   }
//
// 注意 anchor 用的是"目标自己的矩形"（通常是节点 worldBounds），不是指针位置：
// 锚点跟着元素走，滚动、重排时气泡才不会飘。

#include <memory>
#include <string_view>

#include "Painter.h"
#include "TextLayout.h"
#include "TextRenderer.h"
#include "kiriyama/core/Color.h"
#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/Tooltip.h"

namespace kiriyama::render {

/// 气泡的外观。定位参数在 core::TooltipOptions 里，这里只管"长什么样"。
struct TooltipStyle {
    core::Color background{core::Color::Hex(0xFFFFFF)};
    core::Color border{core::Color::Hex(0xD9D6CC)};
    // 注意别写 Color::Hex(0x00000033)：那个字面量数值上就是 0x33，会被 Hex() 当成
    // 6 位色 #000033（实心深蓝）。要带 alpha 用 Rgba()。
    core::Color shadowColor{core::Color::Rgba(0.0, 0.0, 0.0, 0.18)};
    core::Color titleColor{core::Color::Hex(0x2C2C2A)};
    core::Color bodyColor{core::Color::Hex(0x5C5C56)};

    double radius{6.0};
    double paddingX{10.0};
    double paddingY{8.0};
    /// 标题与正文之间。
    double titleGap{3.0};

    double titleSize{13.0};
    double bodySize{12.0};

    /// 内容最大宽度（含内边距）。超出的文字自动折行。
    double maxWidth{240.0};
    /// 正文最多几行，超出加省略号。
    int maxBodyLines{6};

    /// 投影。纯色偏移阴影，不做高斯模糊（SD 没有 blur，这层不值得上离屏）。
    bool shadow{true};
};

/// 气泡内容。标题可空，正文可空，两边都空就不画。
struct TooltipContent {
    std::string_view title{};
    std::string_view body{};
};

class TooltipRenderer {
public:
    TooltipRenderer(Painter& painter, SdlTextRenderer& text) noexcept;

    TooltipRenderer(const TooltipRenderer&) = delete;
    TooltipRenderer& operator=(const TooltipRenderer&) = delete;

    /// 字体面。不设时用 SdlTextRenderer 当前的默认字体面。
    TooltipRenderer& setFace(std::shared_ptr<const FontFace> face);
    TooltipRenderer& setStyle(TooltipStyle style);
    /// 定位参数（贴哪一侧 / 间距 / 箭头宽度）。cornerRadius 会跟 style.radius 同步。
    TooltipRenderer& setOptions(core::TooltipOptions options);

    [[nodiscard]] const TooltipStyle& style() const noexcept { return style_; }
    [[nodiscard]] const core::TooltipOptions& options() const noexcept { return options_; }

    /// 只量不画，返回卡片尺寸（已含内边距）。想知道气泡会不会挡住别的东西时用。
    [[nodiscard]] core::Vec2d measure(const TooltipContent& content) const;

    /// 量 -> 定位 -> 画。返回卡片矩形（调试 / 命中测试用）。
    [[nodiscard]] core::RectD draw(const core::RectD& anchor, const TooltipContent& content,
                                   const core::RectD& viewport, double opacity = 1.0);

    /// 已经算好 layout 时直接画（自己做动画 / 缓存布局的场景）。
    void drawLayout(const core::TooltipLayout& layout, const TooltipContent& content,
                    double opacity = 1.0);

private:
    struct ContentLayout {
        TextLayout title;
        TextLayout body;
        bool hasTitle{false};
        bool hasBody{false};
    };

    [[nodiscard]] ContentLayout layoutContent(const TooltipContent& content) const;
    [[nodiscard]] Font titleFont() const;
    [[nodiscard]] Font bodyFont() const;
    /// 内容排版能用的宽度（maxWidth 去掉左右内边距）。
    [[nodiscard]] double innerWidth() const noexcept;

    Painter& painter_;
    SdlTextRenderer& text_;
    std::shared_ptr<const FontFace> face_{};
    TooltipStyle style_{};
    core::TooltipOptions options_{};
};

}  // namespace kiriyama::render
