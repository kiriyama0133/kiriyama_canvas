#pragma once

// 动画节点：Animation 是 SceneNode 的子类，包住一棵子树就能整块做动画。
//
// 为什么做成节点，而不是"给节点挂一个动画对象"：
//   - 动画天然按分组执行，一次移动整张卡片，不用逐个子节点设值；
//   - 驱动目标就在节点自己身上（transform / style.opacity），不需要额外句柄；
//   - style.opacity 沿子树向下继承，淡入淡出只动父节点一处即可。
//
// 四条轨道：translation / opacity / scale / rotation。同一属性同一时刻只有一条轨道。
//
// 中断语义（本组件最要紧的部分）：
//   1. 再起一条同属性轨道会**替换**旧的，不会两条叠着算同一份位移。
//   2. 新轨道从"当前值"起步，所以"淡入到一半改淡出"是接着走的，不会跳回起点。
//   3. 重复请求（同目标 + 同时长 + 同曲线）直接忽略，不会把正在跑的动画重置回第一帧。
//      调用方可以每帧无脑调 toOpacity(0)，不必自己记状态——这就是"防止重复动画"。
//   4. stop() 停在当前值、finish() 跳到目标值，两者都不触发 onComplete；
//      只有自然走完的轨道才回调，回调可以安全地在里面再起新动画。
//
// 边界：Animation 独占节点的 scale / rotation 两个分量（翻译仍走 setTranslation，
// 与外部改动兼容）。需要叠加外层变换时，请在 Animation 外面再套一个 Group。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "kiriyama/core/SceneNode.h"
#include "kiriyama/math/Easing.h"
#include "kiriyama/scene/Widget.h"

namespace kiriyama::scene {

using core::Affine2D;
using core::NodeType;
using core::SceneNode;
using core::Vec2d;

/// 可被驱动的属性。同属性即"同一条轨道"。
enum class AnimProperty : std::uint8_t {
    Translation,
    Opacity,
    Scale,
    Rotation,
};

/// 一次动画请求的参数。
struct AnimSpec {
    /// 时长（秒）。< 0 表示用 Animation 的默认时长；== 0 表示立即到位。
    double duration{-1.0};
    math::Ease ease{math::Ease::OutCubic};
    /// 延迟（秒）。延迟期间属性保持原值。
    double delay{0.0};
    /// 自然完成时回调（stop / finish 不触发）。
    std::function<void()> done{};
};

class Animation : public Widget {
public:
    explicit Animation(std::string key = {});

    Animation(const Animation&) = delete;
    Animation& operator=(const Animation&) = delete;
    Animation(Animation&&) = delete;
    Animation& operator=(Animation&&) = delete;

    // ------------------------------------------------------------ 默认参数

    /// 未显式给 duration 时使用的时长。默认 0.2s。
    void setDefaultDuration(double seconds) noexcept;
    [[nodiscard]] double defaultDuration() const noexcept { return defaultDuration_; }

    // ------------------------------------------------------------ 起动画
    // 全部返回 *this，方便链式写：anim.toOpacity(1).toTranslation({0, 0});

    Animation& toTranslation(const Vec2d& target, AnimSpec spec = {});
    /// 相对当前值位移。
    Animation& byTranslation(const Vec2d& delta, AnimSpec spec = {});

    Animation& toOpacity(double target, AnimSpec spec = {});
    Animation& toScale(const Vec2d& target, AnimSpec spec = {});
    Animation& toRotation(double radians, AnimSpec spec = {});

    // ------------------------------------------------------------ 查询

    [[nodiscard]] bool animating(AnimProperty property) const noexcept;
    [[nodiscard]] bool animating() const noexcept { return !tracks_.empty(); }
    [[nodiscard]] std::size_t trackCount() const noexcept { return tracks_.size(); }

    /// 该属性的进度 0..1；没有轨道时返回 1（视为已到位）。
    [[nodiscard]] double progress(AnimProperty property) const noexcept;

    // ------------------------------------------------------------ 控制

    /// 停在当前值，不触发回调。
    void stop(AnimProperty property) noexcept;
    void stopAll() noexcept;

    /// 立刻跳到目标值并移除轨道（不触发回调）。
    void finish(AnimProperty property) noexcept;
    void finishAll() noexcept;

    // ------------------------------------------------------------ 推进

    /// 按秒推进。有属性被改动返回 true（渲染层据此标脏 / 重绘）。
    bool update(double deltaSeconds);

protected:
    /// 交给基类的 tickTree 驱动：宿主不用单独记得给动画节点喂 dt。
    void onUpdate(const FrameContext& ctx) override { update(ctx.deltaTime); }

private:
    struct Track {
        AnimProperty property{AnimProperty::Opacity};
        Vec2d from{};
        Vec2d to{};
        double delay{0.0};
        double duration{0.0};
        math::Ease ease{math::Ease::OutCubic};
        /// 从起动画那一刻开始累计，含 delay。
        double elapsed{0.0};
        std::function<void()> done{};
    };

    void start(AnimProperty property, const Vec2d& to, const AnimSpec& spec);
    void apply(AnimProperty property, const Vec2d& value);
    [[nodiscard]] Vec2d current(AnimProperty property) const noexcept;
    [[nodiscard]] Track* find(AnimProperty property) noexcept;
    [[nodiscard]] const Track* find(AnimProperty property) const noexcept;

    /// scale / rotation 是 Animation 自己的状态，改动时要重建 TRS。
    void applyTrs();

    std::vector<Track> tracks_;
    double defaultDuration_{0.2};
    Vec2d scale_{1.0, 1.0};
    double rotation_{0.0};
};

}  // namespace kiriyama::scene
