#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "kiriyama/math/Easing.h"

namespace {

using kiriyama::math::ease;
using kiriyama::math::Ease;

constexpr std::array<Ease, 13> kAllCurves{
    Ease::Linear,     Ease::InQuad,    Ease::OutQuad,     Ease::InOutQuad,
    Ease::InCubic,    Ease::OutCubic,  Ease::InOutCubic,  Ease::InSine,
    Ease::OutSine,    Ease::InOutSine, Ease::Smoothstep,  Ease::Smootherstep,
    Ease::OutBack,
};

TEST(Easing, HitsBothEndpoints) {
    for (const Ease kind : kAllCurves) {
        EXPECT_NEAR(ease(kind, 0.0), 0.0, 1e-12);
        EXPECT_NEAR(ease(kind, 1.0), 1.0, 1e-12) << "curve " << static_cast<int>(kind);
    }
}

TEST(Easing, ClampsInput) {
    EXPECT_NEAR(ease(Ease::Linear, -5.0), 0.0, 1e-12);
    EXPECT_NEAR(ease(Ease::Linear, 5.0), 1.0, 1e-12);
    EXPECT_NEAR(ease(Ease::OutQuad, -0.5), 0.0, 1e-12);
}

TEST(Easing, KnownValues) {
    EXPECT_NEAR(ease(Ease::Linear, 0.25), 0.25, 1e-12);
    EXPECT_NEAR(ease(Ease::InQuad, 0.5), 0.25, 1e-12);
    EXPECT_NEAR(ease(Ease::OutQuad, 0.5), 0.75, 1e-12);
    EXPECT_NEAR(ease(Ease::InCubic, 0.5), 0.125, 1e-12);
    EXPECT_NEAR(ease(Ease::OutCubic, 0.5), 0.875, 1e-12);
    EXPECT_NEAR(ease(Ease::InOutQuad, 0.25), 0.125, 1e-12);
    EXPECT_NEAR(ease(Ease::Smoothstep, 0.5), 0.5, 1e-12);
    EXPECT_NEAR(ease(Ease::Smootherstep, 0.5), 0.5, 1e-12);
}

TEST(Easing, MonotonicExceptForOvershoot) {
    const std::array<Ease, 11> monotonic{Ease::Linear,     Ease::InQuad,    Ease::OutQuad,
                                         Ease::InOutQuad,  Ease::InCubic,   Ease::OutCubic,
                                         Ease::InOutCubic, Ease::InSine,    Ease::OutSine,
                                         Ease::InOutSine,  Ease::Smoothstep};
    for (const Ease kind : monotonic) {
        double previous = 0.0;
        for (int i = 1; i <= 20; ++i) {
            const double value = ease(kind, i / 20.0);
            EXPECT_GE(value, previous - 1e-12) << "curve " << static_cast<int>(kind) << " at " << i;
            previous = value;
        }
    }
}

TEST(Easing, OutBackOvershootsThenSettles) {
    double peak = 0.0;
    for (int i = 0; i <= 40; ++i) {
        peak = std::max(peak, ease(Ease::OutBack, i / 40.0));
    }
    EXPECT_GT(peak, 1.0);  // 过冲是它的用途
    EXPECT_LT(peak, 1.2);  // 但不能夸张到像 bug
    EXPECT_NEAR(ease(Ease::OutBack, 1.0), 1.0, 1e-12);
}

}  // namespace
