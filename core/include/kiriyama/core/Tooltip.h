#pragma once

// Tooltip：气泡定位 + 显隐状态机（hover / focus 触发）。
//
// 和 Interaction 一样分两层：
//   本模块   纯几何 + 纯时间。不认识 SDL，也不知道文字有多宽。
//   src 层   量文字 -> 算 contentSize -> 调 computeTooltipLayout -> 用 Painter 落笔。
//
// 两个入口：
//   computeTooltipLayout()  给定"锚点矩形 + 内容尺寸 + 视口"，算出卡片矩形、实际贴哪
//                           一侧、箭头落点。翻面、贴边夹取都在这里，是纯函数，可单测。
//   TooltipState            只管"什么时候显示哪个目标"：hover 延迟、移开延迟、focus 立即
//                           显示、切换目标。它不持有场景树，也不关心长什么样。
//
// 为什么不把文字度量放进 core：core 要能跑在没有 fontconfig / 没有渲染器的环境里。
// 内容尺寸由调用方（src 层的 TooltipRenderer）量好后传进来，core 只做矩形运算。

#include <cstdint>

#include "Geometry.h"
#include "kiriyama/math/Vector2D.h"

namespace kiriyama::core {

/// 气泡贴在锚点的哪一侧。
enum class TooltipSide : std::uint8_t { Above, Below, Left, Right };

/// 沿那一侧怎么对齐。
enum class TooltipAlign : std::uint8_t { Start, Center, End };

struct TooltipOptions {
    /// 期望贴在哪一侧；放不下时会翻面。
    TooltipSide side{TooltipSide::Above};
    TooltipAlign align{TooltipAlign::Center};

    /// 锚点与卡片之间的间距。箭头就活在这段距离里（长度 = gap）。
    double gap{8.0};
    /// 沿边方向的额外平移：上下侧为 +x，左右侧为 +y。
    double offset{0.0};
    /// 卡片与视口边缘的最小留白。
    double margin{4.0};

    /// 这一侧放不下就翻到对面。
    bool flip{true};
    /// 翻面也放不下就夹进视口。
    bool clampToViewport{true};

    /// 箭头底边宽度，0 = 不画箭头。
    double arrowWidth{10.0};
    /// 卡片圆角。箭头落点会避开圆角区域。
    double cornerRadius{6.0};

    [[nodiscard]] constexpr bool operator==(const TooltipOptions&) const noexcept = default;
};

/// 定位结果。renderer 只照着它画，不再做任何判断。
struct TooltipLayout {
    bool visible{false};

    /// 卡片矩形（画布 / 逻辑坐标）。
    RectD rect{};
    /// 实际贴的那一侧（可能是翻面后的）。
    TooltipSide side{TooltipSide::Above};
    /// 是否因为放不下而翻到了对面。
    bool flipped{false};
    /// 是否因为超出视口而被夹回来了。
    bool clamped{false};

    /// 箭头：tip 指向锚点，base 落在卡片边上。hasArrow 为 false 时两个点无意义。
    bool hasArrow{false};
    Vec2d arrowTip{};
    Vec2d arrowBase{};
    /// 箭头被夹到卡片边缘（锚点偏到卡片角外）——画的时候可以视作退化。
    bool arrowClamped{false};
};

/// 算气泡位置。
///
/// anchor       锚点矩形（世界 / 画布坐标都行，只要和 viewport 同一坐标系）
/// contentSize  内容尺寸，**已经含内边距**。高度宽度都 <= 0 时返回 visible = false。
/// viewport     可见区域（通常是画布矩形）
[[nodiscard]] TooltipLayout computeTooltipLayout(const RectD& anchor, const Vec2d& contentSize,
                                                 const RectD& viewport,
                                                 const TooltipOptions& options = {}) noexcept;

/// 箭头三角形的三个顶点（tip, base 两侧）。没有箭头时返回 false 且不写 points。
/// 单独拎出来，是因为 renderer 里"填充 + 描边"要复用同一组顶点。
[[nodiscard]] bool tooltipArrowPolygon(const TooltipLayout& layout, double arrowWidth,
                                       Vec2d outPoints[3]) noexcept;

// ---------------------------------------------------------------- 状态机

/// 时间参数，单位毫秒。全部相对调用方传进来的 nowMs（单调时钟即可）。
struct TooltipTiming {
    /// 悬停多久后出现。太小会一路划过就弹一堆。
    double showDelay{350.0};
    /// 移开后多久消失。留一点时间给"指针移进气泡"这种操作。
    double hideDelay{80.0};
    /// 键盘 focus 的延迟，默认立即。
    double focusDelay{0.0};
    /// 淡入时长；0 = 不做淡入。
    double fadeIn{120.0};
    /// 指针按下时是否收起（拖拽、点击时挡住目标很烦）。
    bool hideOnPress{true};
};

/// update() 的返回值：只报告"可见性 / 目标"变了，渲染层据此决定要不要重画这一层。
struct TooltipChange {
    bool changed{false};
    bool visible{false};
    std::uint64_t targetId{0};
};

/// 什么时候显示哪个气泡。
///
/// 规则（按优先级）：
///   1. focus 目标永远压过 hover —— 键盘用户的操作不该被路过的鼠标顶掉。
///   2. 同一个 id 反复喂不会重置计时（指针在同一个目标上移动不该一直不弹出）。
///   3. 换目标要重新等一次延迟；从"有"变"没有"走 hideDelay。
///
/// 它不持有 anchor 的副本给渲染层"随便读"——anchor 可能每帧都在变（滚动、重排），
/// 调用方每帧 hover() 一次即可，id 不变就不会重置计时。
class TooltipState {
public:
    /// 没有截止时间。
    static constexpr double kNoDeadline = -1.0e300;

    TooltipState() = default;
    explicit TooltipState(TooltipTiming timing) noexcept : timing_(timing) {}

    [[nodiscard]] const TooltipTiming& timing() const noexcept { return timing_; }
    void setTiming(TooltipTiming timing) noexcept { timing_ = timing; }

    // ---- 输入（可以每帧调，也可以只在事件里调）

    /// 指针当前悬停的目标。id = 0 表示"没悬停任何东西"（等价于 leave）。
    void hover(std::uint64_t id, const RectD& anchor, double nowMs) noexcept;

    /// 键盘焦点落到某个目标。id = 0 等价于 blur()。
    void focus(std::uint64_t id, const RectD& anchor, double nowMs) noexcept;

    /// 焦点离开。
    void blur(double nowMs) noexcept;

    /// 指针按下。hideOnPress 打开时立刻收起当前气泡并取消 hover。
    [[nodiscard]] TooltipChange press(double nowMs) noexcept;

    /// 立即收起（开始滚动 / 拖拽 / 数据刷新）。hover 一并作废，等指针重新进入。
    [[nodiscard]] TooltipChange hide() noexcept;

    /// 清空全部状态（窗口失焦、场景整体重建）。
    void reset() noexcept;

    // ---- 时间推进

    /// 每帧调一次；返回本次是否发生了"可见 / 目标"变化。
    [[nodiscard]] TooltipChange update(double nowMs) noexcept;

    // ---- 查询

    [[nodiscard]] bool visible() const noexcept { return visibleId_ != 0; }
    [[nodiscard]] std::uint64_t targetId() const noexcept { return visibleId_; }
    [[nodiscard]] const RectD& anchor() const noexcept { return anchor_; }
    [[nodiscard]] bool focused() const noexcept { return focusId_ != 0; }

    /// 淡入进度 0~1。不可见时返回 0。
    [[nodiscard]] double opacity(double nowMs) const noexcept;

private:
    [[nodiscard]] TooltipChange show(std::uint64_t id, double nowMs) noexcept;
    [[nodiscard]] TooltipChange hideNow() noexcept;

    TooltipTiming timing_{};

    std::uint64_t hoverId_{0};
    double hoverSince_{0.0};
    RectD hoverAnchor_{};

    std::uint64_t focusId_{0};
    double focusSince_{0.0};
    RectD focusAnchor_{};

    std::uint64_t visibleId_{0};
    double shownAt_{0.0};
    /// 计划收起的时刻；kNoDeadline 表示没有在倒计时。
    double hideAt_{kNoDeadline};

    /// 当前可见目标的锚点（hover 或 focus 里挑那个生效的）。
    RectD anchor_{};
};

}  // namespace kiriyama::core
