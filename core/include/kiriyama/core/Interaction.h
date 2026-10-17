#pragma once

// 交互状态：hover / active（按下）/ click，纯逻辑，不依赖任何窗口系统。
//
// 分工：
//   src 层  把 SDL 事件翻译成 PointerEvent（画布坐标），喂给 apply()
//   本模块  做命中测试、维护 hover / active / press 状态、算出要重绘的节点
//   渲染层  画每个节点前调 styleFor(node)，把叠加样式合进 SceneStyle
//
// 关键约定：
//   - 交互状态**不写进** SceneNode 的样式里。节点的 SceneStyle 始终是"数据样式"，
//     交互只是渲染时叠加一层。否则 diff 会把 hover 色当成数据的一部分，下次对账就抹不掉。
//   - 唯一的写操作是 markDirty()：hover / active 变化的节点会被标脏，供增量重绘定位。
//   - 按下期间 hover 冻结在按下的节点上（等价于指针捕获），松手后才重新命中。
//   - 节点被 diff 删掉后，残留的 id 会在下一次 apply() 时自动清理。

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "SceneNode.h"
#include "SceneQuery.h"
#include "kiriyama/math/Transform2D.h"

namespace kiriyama::core {

// ---------------------------------------------------------------- 输入

/// 指针事件类型。Enter / Leave 可以由 src 用窗口边界事件产生，
/// 也可以只发 Move —— apply() 会自己按命中结果判断进出。
enum class PointerEventType : std::uint8_t {
    Enter,
    Move,
    Leave,
    Down,
    Up,
    Cancel,  ///< 焦点丢失 / 系统取消，不发 click
};

struct PointerEvent {
    PointerEventType type{PointerEventType::Move};
    /// 画布坐标（左上角原点，y 向下）。有 viewTransform 时按逆变换换到世界坐标。
    Vec2d position{};
    /// 只有 Down / Up 有意义：0 = 左键，1 = 右键，-1 = 任意键。
    int button{0};
};

// ---------------------------------------------------------------- 样式叠加

/// 交互样式：描述"相对原始样式的增量"，不替换节点自身的 SceneStyle。
struct InteractionStyle {
    /// 叠在填充之上的颜色（a == 0 表示不改）。
    Color overlayFill{};
    /// 替换描边颜色（a == 0 表示不改）。
    Color overlayStroke{};
    /// 在原描边宽度上加的量；没有 overlayStroke 时无效。
    double strokeWidthDelta{0.0};
    /// 乘在原 opacity 上。
    double opacityScale{1.0};
    /// 以局部包围盒中心缩放，1 = 不缩放。注意会超出原始包围盒，剪枝要留余量。
    double scale{1.0};
    /// 命中此类节点时，src 应把光标切成手型。
    bool pointerCursor{false};

    [[nodiscard]] constexpr bool operator==(const InteractionStyle&) const noexcept = default;
};

// ---------------------------------------------------------------- 选项

struct InteractionOptions {
    InteractionStyle hover{};
    InteractionStyle active{};

    /// 命中放宽像素。细线 / 小点靠它才点得中。
    double hitTolerance{3.0};

    /// 交互锚点层级：0 = 命中的节点本身，N = 沿父链向上 N 层（越界取根）。
    /// 图表里常用的用法：柱形落在 group 里，设 1 就是"整组高亮"。
    /// 锚点落在 Group 上时，叠加样式会向所有后代继承（见 styleFor）。
    int anchorLevel{0};

    /// 是否维护 hoverChain()（根 -> 锚点的 id 链），tooltip 定位和事件冒泡要用。
    bool trackChain{true};
};

// ---------------------------------------------------------------- 变化

/// apply() 的返回值：本次事件造成的状态变化。src 可以据此决定要不要重绘 / 改光标。
struct InteractionChange {
    bool hoverChanged{false};
    bool activeChanged{false};
    bool pointerMoved{false};
    bool pointerInside{false};

    std::uint64_t hoveredId{0};  ///< 当前的 hover 锚点，0 表示没有
    std::uint64_t activeId{0};   ///< 当前按下的锚点，0 表示没有
    std::uint64_t releasedId{0}; ///< 本次 Up / Cancel 释放掉的 active，0 表示没有
    std::uint64_t clickedId{0};  ///< 本次 Up 落在 Down 的同一锚点上

    /// 需要重绘的节点（旧锚点 + 新锚点，已去重）。调用方也可以忽略它，
    /// 直接用 collectDirtyNodes() 扫全树。
    std::vector<std::uint64_t> dirtyIds;

    [[nodiscard]] bool needsRepaint() const noexcept { return hoverChanged || activeChanged; }
    [[nodiscard]] bool clicked() const noexcept { return clickedId != 0; }
};

// ---------------------------------------------------------------- 状态

class InteractionState {
public:
    InteractionState() = default;
    explicit InteractionState(InteractionOptions options);

    [[nodiscard]] const InteractionOptions& options() const noexcept { return options_; }
    void setOptions(InteractionOptions options);

    /// 绘制时施加在根节点上的变换（世界 -> 画布）。事件坐标会按它的逆变换换回世界坐标。
    /// 没有平移 / 缩放时不用设。
    void setViewTransform(const math::Affine2D& worldToCanvas);
    void clearViewTransform() noexcept;
    [[nodiscard]] std::optional<math::Affine2D> viewTransform() const noexcept {
        return viewTransform_;
    }

    /// 唯一入口：喂一个事件 + 当前渲染树，返回本次变化。
    /// root 需要是可写的，因为变化的节点会被 markDirty()。
    InteractionChange apply(SceneNode& root, const PointerEvent& event);

    /// 清空所有状态（窗口失焦、树整体重建时用）。
    void reset();

    // ---- 查询

    [[nodiscard]] bool hovered() const noexcept { return hoveredId_ != 0; }
    [[nodiscard]] std::uint64_t hoveredId() const noexcept { return hoveredId_; }
    [[nodiscard]] bool pressed() const noexcept { return activeId_ != 0; }
    [[nodiscard]] std::uint64_t activeId() const noexcept { return activeId_; }
    /// 当前按下的鼠标键（0 左 / 1 右 / -1 任意）；没按下时为 0。
    [[nodiscard]] int pressedButton() const noexcept { return pressedButton_; }
    [[nodiscard]] Vec2d pointer() const noexcept { return pointer_; }
    [[nodiscard]] bool pointerInside() const noexcept { return pointerInside_; }

    /// 根 -> 锚点的 id 链；没有 hover 时为空。
    [[nodiscard]] std::span<const std::uint64_t> hoverChain() const noexcept {
        return hoverChain_;
    }

    /// 节点或它的某个祖先是不是当前 hover 锚点。Group 上的 hover 因此能传导给后代。
    [[nodiscard]] bool isHovered(const SceneNode& node) const noexcept;
    [[nodiscard]] bool isActive(const SceneNode& node) const noexcept;

    /// 该节点该用哪套叠加样式。active 优先于 hover。
    [[nodiscard]] InteractionStyle styleFor(const SceneNode& node) const noexcept;

    /// hover 锚点是否要求手型光标。
    [[nodiscard]] bool wantsPointerCursor() const noexcept;

private:
    [[nodiscard]] Vec2d toWorld(const Vec2d& canvasPoint) const noexcept;
    /// 清掉树里已经不存在的 id（diff 删节点后残留的悬空 id）。
    void pruneMissing(SceneNode& root, InteractionChange& change);
    /// 按 anchorLevel 把命中节点上提到锚点。
    [[nodiscard]] SceneNode* pickAnchor(SceneNode* node) const noexcept;
    /// 命中指定世界坐标，返回锚点（没有命中返回 nullptr）。
    [[nodiscard]] SceneNode* hitAnchor(SceneNode& root, const Vec2d& worldPoint);

    InteractionChange setHover(SceneNode& root, SceneNode* anchor, InteractionChange& change);
    InteractionChange setActive(SceneNode& root, SceneNode* anchor, InteractionChange& change);

    InteractionOptions options_{};
    std::optional<math::Affine2D> viewTransform_{};

    Vec2d pointer_{};
    bool pointerInside_{false};

    std::uint64_t hoveredId_{0};
    std::vector<std::uint64_t> hoverChain_;

    std::uint64_t activeId_{0};
    std::uint64_t downId_{0};  ///< 按下时的锚点，用来判断 click
    int pressedButton_{0};
};

// ---------------------------------------------------------------- 样式合成

/// 把叠加样式合进节点原始样式，得到最终要画的样式。
[[nodiscard]] SceneStyle applyInteractionStyle(const SceneStyle& base,
                                               const InteractionStyle& overlay) noexcept;

/// 叠加样式的局部变换（以包围盒中心缩放）。scale == 1 时返回单位矩阵。
[[nodiscard]] math::Affine2D interactionTransform(const SceneNode& node,
                                                  const InteractionStyle& overlay) noexcept;

}  // namespace kiriyama::core
