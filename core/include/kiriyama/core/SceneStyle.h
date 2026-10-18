#pragma once

// 节点样式：填充、描边、文字排版。
//
// 一份样式同时覆盖所有图元，用不到的字段忽略——好处是渲染层不需要按类型
// 维护多套样式结构，改一个字段的代价是固定的。

#include <cstdint>
#include <memory>
#include <vector>

#include "Color.h"
#include "Theme.h"

namespace kiriyama::core {

enum class TextAlign : std::uint8_t { Left, Center, Right };
enum class TextBaseline : std::uint8_t { Top, Middle, Bottom };

/// 光标形状。节点上写的是"命中我时想要什么光标"：
///   Inherit（默认）不是一种形状，而是"跟随父节点"；
///   一路向上都是 Inherit 时用 Default。这样绝大多数节点完全不用管光标。
///
/// 放在 SceneStyle 而不是 SceneNode 的独立字段，是因为它和 fill / fontSize 一样，
/// 属于"这个节点长什么样、怎么表现"，而不是树结构本身。
enum class CursorShape : std::uint8_t {
    Inherit,
    Default,
    Pointer,  ///< 可点击（表头、按钮）
    Text,     ///< 可选中文字
    ResizeH,  ///< 拖列宽
    ResizeV,
    Grab,      ///< 可拖拽
    Grabbing,  ///< 正在拖
    NotAllowed,
};

struct SceneStyle {
    /// 语义 token 或直接色（见 Theme.h）。token != None 时渲染层用当前主题解析，
    /// 所以切主题不需要重建场景树；直接色用于表格样式、组件自定义色这类
    /// "调用方明确给了颜色"的场合。
    Paint fill{};
    Paint stroke{};
    double strokeWidth{0.0};
    double opacity{1.0};         ///< 作用于整棵子树
    double cornerRadius{0.0};    ///< 仅 Rect
    double fontSize{12.0};       ///< 仅 Text
    double lineHeight{0.0};      ///< 仅 Text，0 表示按 fontSize 推算
    TextAlign textAlign{TextAlign::Left};
    TextBaseline textBaseline{TextBaseline::Top};
    /// 仅 Text：0 = 不限行数；1 = 单行，超宽按 ellipsis 截断（表格单元格用）。
    int maxLines{0};
    std::string ellipsis{"..."};
    /// 命中本节点时的光标。Inherit = 问父节点。
    CursorShape cursor{CursorShape::Inherit};
    /// 局部系列色板覆盖：非空时，子树里 token == Series 的 Paint 用它解析
    ///（沿树继承，最近祖先优先）。组件级自定义色板（如 PieChart::setPalette）
    /// 挂在对账根组上；空 = 用全局主题的 series 色板。
    std::shared_ptr<const std::vector<Color>> seriesPalette{};

    [[nodiscard]] constexpr bool hasFill() const noexcept { return fill.visible(); }
    [[nodiscard]] constexpr bool hasStroke() const noexcept {
        return stroke.visible() && strokeWidth > 0.0;
    }

    [[nodiscard]] constexpr bool operator==(const SceneStyle&) const noexcept = default;
};

}  // namespace kiriyama::core
