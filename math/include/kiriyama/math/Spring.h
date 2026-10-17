#pragma once

// 二阶弹簧振子（阻尼简谐振子）模型。
//
// 方程：m·x'' + c·x' + k·(x - target) = 0
//   m = mass 质量，k = stiffness 刚度，c = damping 阻尼
//   ω₀ = √(k/m)                固有角频率
//   ζ  = c / (2·√(k·m))        阻尼比
//
// ζ < 1 欠阻尼（会回弹，图表交互手感来源）
// ζ = 1 临界阻尼（最快且不过冲，用于"稳"的画面）
// ζ > 1 过阻尼（慢，几乎不回弹）
//
// 推进用解析解而不是欧拉积分：线性系统可以任意步长精确求解，
// 掉帧 / 标签页休眠回来都不会炸，也不会因为 dt 大而发散。

#include <cmath>

#include "MathUtils.h"

namespace kiriyama::math {

enum class SpringDamping {
    Underdamped,       // 欠阻尼：会过冲、来回摆
    CriticallyDamped,  // 临界阻尼：不过冲的最快收敛
    Overdamped,        // 过阻尼：不摆动但收敛慢
};

struct SpringParams {
    double mass = 1.0;
    double stiffness = 120.0;
    double damping = 12.0;

    /// ω₀ = √(k/m)。k 或 m 非法时返回 0。
    [[nodiscard]] double angularFrequency() const noexcept;

    /// ζ = c / (2·√(k·m))。
    [[nodiscard]] double dampingRatio() const noexcept;

    [[nodiscard]] SpringDamping classification() const noexcept;

    /// mass > 0、stiffness > 0、damping >= 0 且均为有限值。
    [[nodiscard]] bool usable() const noexcept;

    /// 按"固有频率 + 阻尼比"反推质量/刚度/阻尼（取 mass = 1）。
    /// frequencyHz 是每秒震荡次数，dampingRatio = 1 为临界阻尼。
    [[nodiscard]] static SpringParams FromFrequency(double frequencyHz,
                                                    double dampingRatio = 1.0) noexcept;
};

/// 相对目标的位移与速度。
struct SpringState {
    double displacement = 0.0;  // x - target
    double velocity = 0.0;

    [[nodiscard]] constexpr bool operator==(const SpringState&) const noexcept = default;
};

/// 解析推进 dt 秒。dt <= 0 或参数非法时原样返回。
[[nodiscard]] SpringState springAdvance(SpringState state, const SpringParams& params,
                                       double dt) noexcept;

/// 衰减包络落进 tolerance 所需时间的估计值（用于排期/判断动画是否该收尾）。
/// 无阻尼时返回 +∞。
[[nodiscard]] double springSettleTime(const SpringState& state, const SpringParams& params,
                                      double tolerance = 1e-3) noexcept;

/// 有状态的弹簧：持有 target / position / velocity，支持中途改目标（保留速度）。
class Spring {
public:
    Spring() noexcept = default;
    explicit Spring(SpringParams params) noexcept;

    void setParams(SpringParams params) noexcept;
    [[nodiscard]] const SpringParams& params() const noexcept { return params_; }

    /// 改目标，当前位置与速度保留——这就是"打断"手感不僵硬的原因。
    void setTarget(double target) noexcept;
    [[nodiscard]] double target() const noexcept { return target_; }

    void setPosition(double position) noexcept;  // 保留速度
    [[nodiscard]] double position() const noexcept { return target_ + state_.displacement; }

    void setVelocity(double velocity) noexcept;
    [[nodiscard]] double velocity() const noexcept { return state_.velocity; }

    /// 推进 dt 秒，返回新位置。
    double update(double deltaTime) noexcept;

    /// 立刻到位并清零速度。
    void snapToTarget() noexcept;

    /// 位移与速度都低于容差即视为静止。
    [[nodiscard]] bool isAtRest(double positionTolerance = 1e-3,
                                double velocityTolerance = 1e-3) const noexcept;

    [[nodiscard]] double settleTime(double tolerance = 1e-3) const noexcept;

private:
    SpringParams params_{};
    SpringState state_{};
    double target_{0.0};
};

}  // namespace kiriyama::math
