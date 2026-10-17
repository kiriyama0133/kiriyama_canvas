#include "kiriyama/math/Spring.h"

#include <cmath>
#include <limits>

namespace kiriyama::math {
namespace {

/// ζ 与 1 的差距小于该值时按临界阻尼处理。
/// 否则 1/ω_d、1/β 会把浮点误差放大到不可信。
constexpr double kCriticalBand = 1e-4;

/// 参数下限，避免 0 质量 / 0 刚度导致除零。
constexpr double kMinMass = 1e-6;
constexpr double kMinStiffness = 1e-9;

}  // namespace

double SpringParams::angularFrequency() const noexcept {
    if (!(mass > 0.0) || !(stiffness > 0.0)) {
        return 0.0;
    }
    return std::sqrt(stiffness / mass);
}

double SpringParams::dampingRatio() const noexcept {
    // 临界阻尼 c_crit = 2·√(k·m) = 2·m·ω₀
    const double critical = 2.0 * angularFrequency() * mass;
    return critical > 0.0 ? damping / critical : 0.0;
}

SpringDamping SpringParams::classification() const noexcept {
    const double zeta = dampingRatio();
    if (std::abs(zeta - 1.0) <= kCriticalBand) {
        return SpringDamping::CriticallyDamped;
    }
    return zeta < 1.0 ? SpringDamping::Underdamped : SpringDamping::Overdamped;
}

bool SpringParams::usable() const noexcept {
    return std::isfinite(mass) && std::isfinite(stiffness) && std::isfinite(damping) &&
           mass > 0.0 && stiffness > 0.0 && damping >= 0.0;
}

SpringParams SpringParams::FromFrequency(double frequencyHz, double dampingRatio) noexcept {
    const double omega = 2.0 * kPi * frequencyHz;
    SpringParams params;
    params.mass = 1.0;
    params.stiffness = omega * omega * params.mass;
    params.damping = 2.0 * dampingRatio * omega * params.mass;
    return params;
}

SpringState springAdvance(SpringState state, const SpringParams& params, double dt) noexcept {
    if (!(dt > 0.0) || !params.usable()) {
        return state;
    }

    const double omega = params.angularFrequency();
    const double zeta = params.dampingRatio();
    const double d0 = state.displacement;
    const double v0 = state.velocity;

    double displacement = d0;
    double velocity = v0;

    if (std::abs(zeta - 1.0) <= kCriticalBand) {
        // 临界阻尼： x = (d0 + (v0 + ω₀·d0)·t)·e^(-ω₀t)
        const double decay = std::exp(-omega * dt);
        const double slope = v0 + omega * d0;
        displacement = (d0 + slope * dt) * decay;
        velocity = (v0 - omega * slope * dt) * decay;
    } else if (zeta < 1.0) {
        // 欠阻尼： x = e^(-αt)·(d0·cos(ω_d t) + ((v0 + α·d0)/ω_d)·sin(ω_d t))
        const double alpha = zeta * omega;
        const double dampedOmega = omega * std::sqrt(1.0 - zeta * zeta);
        const double decay = std::exp(-alpha * dt);
        const double cosPart = std::cos(dampedOmega * dt);
        const double sinPart = std::sin(dampedOmega * dt);
        const double sineAmplitude = (v0 + alpha * d0) / dampedOmega;

        displacement = decay * (d0 * cosPart + sineAmplitude * sinPart);
        velocity = decay * (v0 * cosPart -
                            ((omega * omega * d0 + alpha * v0) / dampedOmega) * sinPart);
    } else {
        // 过阻尼：两个实根，x = c₁·e^(r₁t) + c₂·e^(r₂t)
        const double beta = omega * std::sqrt(zeta * zeta - 1.0);
        const double r1 = -zeta * omega + beta;
        const double r2 = -zeta * omega - beta;
        const double denominator = r1 - r2;  // 2β > 0
        const double c1 = (v0 - r2 * d0) / denominator;
        const double c2 = (r1 * d0 - v0) / denominator;
        const double e1 = std::exp(r1 * dt);
        const double e2 = std::exp(r2 * dt);

        displacement = c1 * e1 + c2 * e2;
        velocity = r1 * c1 * e1 + r2 * c2 * e2;
    }

    if (!std::isfinite(displacement) || !std::isfinite(velocity)) {
        // 极端参数下宁可直接收敛，也不让 NaN 流进渲染管线。
        return SpringState{0.0, 0.0};
    }
    return SpringState{displacement, velocity};
}

double springSettleTime(const SpringState& state, const SpringParams& params,
                        double tolerance) noexcept {
    if (!params.usable() || !(tolerance > 0.0)) {
        return 0.0;
    }
    if (std::abs(state.displacement) <= tolerance && std::abs(state.velocity) <= tolerance) {
        return 0.0;
    }

    const double omega = params.angularFrequency();
    const double zeta = params.dampingRatio();
    if (!(zeta > 0.0)) {
        return std::numeric_limits<double>::infinity();  // 无阻尼永不静止
    }

    const double d0 = state.displacement;
    const double v0 = state.velocity;

    // 先用衰减包络估一个初值，再用解析解迭代修正——
    // 临界/过阻尼的包络不是简单指数，一次估计会有偏差。
    double amplitude = 0.0;
    double decayRate = 0.0;

    if (std::abs(zeta - 1.0) <= kCriticalBand) {
        amplitude = std::abs(d0) + std::abs(v0 + omega * d0) / omega;
        decayRate = omega;
    } else if (zeta < 1.0) {
        const double alpha = zeta * omega;
        const double dampedOmega = omega * std::sqrt(1.0 - zeta * zeta);
        amplitude = std::abs(d0) + std::abs(v0 + alpha * d0) / dampedOmega;
        decayRate = alpha;
    } else {
        const double beta = omega * std::sqrt(zeta * zeta - 1.0);
        const double denominator = 2.0 * beta;
        const double c1 = std::abs((v0 - (-zeta * omega - beta) * d0) / denominator);
        const double c2 = std::abs(((-zeta * omega + beta) * d0 - v0) / denominator);
        amplitude = c1 + c2;
        decayRate = omega * zeta - beta;  // 衰减较慢的那个极点
    }

    if (!(amplitude > tolerance) || !(decayRate > 0.0)) {
        return 0.0;
    }

    double time = std::log(amplitude / tolerance) / decayRate;
    for (int i = 0; i < 4; ++i) {
        const double residual = std::abs(springAdvance(state, params, time).displacement);
        if (residual <= tolerance) {
            break;
        }
        time += std::log(residual / tolerance) / decayRate;
    }
    return time;
}

Spring::Spring(SpringParams params) noexcept {
    setParams(params);
}

void Spring::setParams(SpringParams params) noexcept {
    // 兜底：0 质量 / 0 刚度会让 ω₀ 变 0，物理公式直接除零。
    if (!std::isfinite(params.mass) || params.mass < kMinMass) {
        params.mass = kMinMass;
    }
    if (!std::isfinite(params.stiffness) || params.stiffness < kMinStiffness) {
        params.stiffness = kMinStiffness;
    }
    if (!std::isfinite(params.damping) || params.damping < 0.0) {
        params.damping = 0.0;
    }
    params_ = params;
}

void Spring::setTarget(double target) noexcept {
    if (!std::isfinite(target)) {
        return;
    }
    const double current = position();
    target_ = target;
    state_.displacement = current - target_;
}

void Spring::setPosition(double position) noexcept {
    if (!std::isfinite(position)) {
        return;
    }
    state_.displacement = position - target_;
}

void Spring::setVelocity(double velocity) noexcept {
    if (std::isfinite(velocity)) {
        state_.velocity = velocity;
    }
}

double Spring::update(double deltaTime) noexcept {
    state_ = springAdvance(state_, params_, deltaTime);
    return position();
}

void Spring::snapToTarget() noexcept {
    state_ = SpringState{0.0, 0.0};
}

bool Spring::isAtRest(double positionTolerance, double velocityTolerance) const noexcept {
    return std::abs(state_.displacement) <= positionTolerance &&
           std::abs(state_.velocity) <= velocityTolerance;
}

double Spring::settleTime(double tolerance) const noexcept {
    return springSettleTime(state_, params_, tolerance);
}

}  // namespace kiriyama::math
