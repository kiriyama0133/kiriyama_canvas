#pragma once

// 可拖拽组件：Drag 本身就是一个节点，把要拖的内容塞进去即可。
//
//   auto drag = std::make_unique<scene::Drag>("card");
//   drag->setFrame({0, 0, 180, 48});      // 命中区 / 可拖范围
//   drag->appendChild(内容);
//
// 指针按下 / 移动 / 抬起由基类 Widget 统一记账，Drag 只处理"拖"这一件事：
//   - 按下在自己身上才接管（靠 Widget 的 hitTest）；
//   - 位移按父坐标系累加，所以放进带位移 / 缩放的父组里依然跟手；
//   - 起拖后捕获指针，拖出父容器也不会丢事件。
//
// 与动画的分工：Drag 只写自己的 translation。要同时做淡入淡出，把 Animation 套在
// 外面（Animation → Drag → 内容），两个组件各写各的节点，不会抢同一个 transform。

#include <cstdint>
#include <functional>
#include <optional>

#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

/// 允许拖拽的方向。
enum class DragAxis : std::uint8_t {
    Both,
    X,
    Y,
    None,
};

struct DragOptions {
    DragAxis axis{DragAxis::Both};

    /// 活动范围，**父坐标系**。不设表示不限。
    std::optional<RectD> bounds{};

    /// 起拖阈值（像素）：位移超过它才算真的在拖，避免点一下就抖一格。
    double threshold{0.0};

    /// false 时只算位移、不动节点，方便调用方自己决定怎么用（比如拖出连线）。
    bool moveNode{true};
};

class Drag final : public Widget {
public:
    using Handler = std::function<void(const Drag&)>;

    explicit Drag(std::string key = {});

    Drag& setOptions(const DragOptions& options) noexcept;
    [[nodiscard]] const DragOptions& options() const noexcept { return options_; }

    Drag& onBegin(Handler handler);
    Drag& onDrag(Handler handler);
    Drag& onEnd(Handler handler);

    /// 手动接管（世界坐标）。通常不用调，tick() 会自动完成 begin/move/end。
    /// 返回 false 表示指针不在命中区里，没接管。
    bool begin(const Vec2d& worldPointer);
    bool move(const Vec2d& worldPointer);
    /// 返回这一次是否真的拖动过（用来区分点击与拖拽）。
    bool end(const Vec2d& worldPointer);

    /// 无条件放弃，不触发 onEnd。
    void cancel() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }

    /// 本次拖拽累计位移（父坐标）。
    [[nodiscard]] Vec2d delta() const noexcept { return delta_; }
    /// 节点当前的平移分量。
    [[nodiscard]] Vec2d translation() const noexcept;

protected:
    void onPress() override;
    void onUpdate(const FrameContext& ctx) override;

private:
    [[nodiscard]] Vec2d toParent(const Vec2d& world) const noexcept;
    [[nodiscard]] Vec2d clampTranslation(const Vec2d& target) const noexcept;

    DragOptions options_{};

    Handler onBegin_{};
    Handler onDrag_{};
    Handler onEnd_{};

    bool active_{false};
    bool dragging_{false};
    Vec2d grab_{};             ///< 按下点（父坐标）
    Vec2d startTranslation_{}; ///< 按下时节点的平移
    Vec2d delta_{};            ///< 累计位移（父坐标）
};

}  // namespace kiriyama::scene
