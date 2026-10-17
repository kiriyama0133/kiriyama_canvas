#include "kiriyama/math/Easing.h"

namespace kiriyama::math {

double ease(Ease kind, double t) noexcept {
    const double p = saturate(t);
    const double inv = 1.0 - p;

    switch (kind) {
        case Ease::Linear:
            return p;

        case Ease::InQuad:
            return p * p;
        case Ease::OutQuad:
            return 1.0 - inv * inv;
        case Ease::InOutQuad:
            return p < 0.5 ? 2.0 * p * p : 1.0 - 2.0 * inv * inv;

        case Ease::InCubic:
            return p * p * p;
        case Ease::OutCubic:
            return 1.0 - inv * inv * inv;
        case Ease::InOutCubic:
            return p < 0.5 ? 4.0 * p * p * p : 1.0 - 4.0 * inv * inv * inv;

        case Ease::InSine:
            return 1.0 - std::cos(p * kHalfPi);
        case Ease::OutSine:
            return std::sin(p * kHalfPi);
        case Ease::InOutSine:
            return -(std::cos(kPi * p) - 1.0) * 0.5;

        case Ease::Smoothstep:
            return smoothstep(0.0, 1.0, p);
        case Ease::Smootherstep:
            return smootherstep(0.0, 1.0, p);

        case Ease::OutBack: {
            // 1.70158 是"回弹量刚好不显得生硬"的经验常数（Robert Penner）。
            // 公式按 (t-1)^n 展开，这里 inv = 1-t，所以三次项要取负号，
            // 否则 t=0 会算出 1 + c3 + c1 ≈ 5.4。
            constexpr double kOvershoot = 1.70158;
            constexpr double kC3 = kOvershoot + 1.0;
            return 1.0 - kC3 * inv * inv * inv + kOvershoot * inv * inv;
        }
    }
    return p;
}

}  // namespace kiriyama::math
