#pragma once

// 文本排版：把一段 UTF-8 文本折成若干行，算好每行的位置与基线。
//
// 这一层同样不碰 SDL，输出的是纯几何结果：
//   - 行切分（硬换行 + 软换行 / 按单词 / 中文按字断行）
//   - 行内字距、制表位
//   - 行高、基线
//   - 水平对齐（左 / 中 / 右）
//   - 行数上限与省略号截断
//
// 渲染层拿到 TextLayout 后只需要"把每行画在 (x, top) 处"。

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Font.h"
#include "kiriyama/core/Geometry.h"
#include "kiriyama/core/SceneStyle.h"

namespace kiriyama::render {

using core::TextAlign;
using core::TextBaseline;
using core::Vec2d;

struct TextLayoutOptions {
    /// 换行宽度上限，<= 0 表示不限宽（此时 wrap 失效）。
    double maxWidth{0.0};
    /// 是否允许软换行；false 时只有 '\n' 会换行。
    bool wrap{true};

    TextAlign align{TextAlign::Left};
    /// 行高，<= 0 时用字体默认行高。
    double lineHeight{0.0};
    /// 额外字距（像素），可为负。
    double letterSpacing{0.0};
    /// 最大行数，<= 0 不限。超出时最后一行加省略号。
    int maxLines{0};
    /// 截断标记，空串表示不加。
    std::string ellipsis{"..."};
    /// 制表位宽度，<= 0 时按 4 个空格算。
    double tabWidth{0.0};
    /// 是否去掉行尾空白（换行点上的空格不应参与对齐）。
    bool trimLineEnds{true};
};

struct TextLine {
    std::string text;      ///< 该行的 UTF-8 内容（已去行尾空白）
    double width{0.0};     ///< 含字距的墨迹宽度
    double x{0.0};         ///< 相对文本块左边界，已按对齐摆好
    double top{0.0};       ///< 相对文本块顶边
    double baseline{0.0};  ///< 相对文本块顶边的基线 y
};

struct TextLayout {
    std::vector<TextLine> lines;
    double width{0.0};     ///< 最宽一行的墨迹宽度
    double boxWidth{0.0};  ///< 排版盒宽度（有 maxWidth 时等于 maxWidth）
    double height{0.0};
    double lineHeight{0.0};
    FontMetrics metrics{};
    bool truncated{false};  ///< 是否因 maxLines 被截断

    [[nodiscard]] bool empty() const noexcept { return lines.empty(); }
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines.size(); }
};

/// 排版入口。text 里的 '\n' / "\r\n" 是硬换行。
[[nodiscard]] TextLayout layoutText(std::string_view text, const Font& font,
                                    const TextLayoutOptions& options = {});

/// 按基线模式把文本块摆进 frame，返回块的左上角（世界 / 父坐标）。
/// 水平方向对齐由 TextLayout 内部按 maxWidth 处理，这里只负责块的原点。
[[nodiscard]] Vec2d placeTextBox(const core::RectD& frame, const TextLayout& layout,
                                 TextBaseline baseline) noexcept;

}  // namespace kiriyama::render
