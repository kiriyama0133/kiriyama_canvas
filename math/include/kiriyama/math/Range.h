#pragma once

// 一维区间：数据域（domain）、像素域（range）、坐标轴可视范围都用它表示。
// 约定区间为闭区间 [minimum, maximum]，允许 minimum == maximum（退化为单点）。

#include <concepts>
#include <type_traits>

#include "MathUtils.h"

namespace kiriyama::math {

template <Number T = double>
class Range {
public:
    using value_type = T;

    constexpr Range() noexcept = default;
    constexpr Range(T minimum, T maximum) noexcept : minimum_(minimum), maximum_(maximum) {}

    /// 不保证 a <= b 时用这个，会自动排序。
    [[nodiscard]] static constexpr Range FromValues(T a, T b) noexcept {
        return a <= b ? Range{a, b} : Range{b, a};
    }

    [[nodiscard]] static constexpr Range FromValue(T value) noexcept { return Range{value, value}; }

    // ------------------------------------------------------------ 访问

    [[nodiscard]] constexpr T minimum() const noexcept { return minimum_; }
    [[nodiscard]] constexpr T maximum() const noexcept { return maximum_; }
    [[nodiscard]] constexpr T span() const noexcept { return static_cast<T>(maximum_ - minimum_); }
    [[nodiscard]] constexpr T midpoint() const noexcept {
        return static_cast<T>((minimum_ + maximum_) / T{2});
    }

    /// maximum < minimum 视为空区间。
    [[nodiscard]] constexpr bool empty() const noexcept { return maximum_ < minimum_; }

    /// 退化为单点。
    [[nodiscard]] constexpr bool degenerate() const noexcept { return maximum_ == minimum_; }

    constexpr void setMinimum(T value) noexcept { minimum_ = value; }
    constexpr void setMaximum(T value) noexcept { maximum_ = value; }

    [[nodiscard]] constexpr bool operator==(const Range& rhs) const noexcept = default;

    // ------------------------------------------------------------ 判定

    [[nodiscard]] constexpr bool contains(T value) const noexcept {
        return !empty() && value >= minimum_ && value <= maximum_;
    }

    [[nodiscard]] constexpr bool contains(const Range& other) const noexcept {
        return other.empty() || (other.minimum_ >= minimum_ && other.maximum_ <= maximum_);
    }

    [[nodiscard]] constexpr bool overlaps(const Range& other) const noexcept {
        return !empty() && !other.empty() && minimum_ <= other.maximum_ &&
               other.minimum_ <= maximum_;
    }

    [[nodiscard]] constexpr bool nearlyEquals(const Range& other,
                                              T epsilon = static_cast<T>(kEpsilon)) const noexcept {
        return nearlyEqual(minimum_, other.minimum_, epsilon) &&
               nearlyEqual(maximum_, other.maximum_, epsilon);
    }

    // ------------------------------------------------------------ 变换

    /// 把 value 夹进区间。
    [[nodiscard]] constexpr T clamp(T value) const noexcept {
        return math::clamp(value, minimum_, maximum_);
    }

    /// 归一化到 [0, 1]。区间退化时返回 0。
    [[nodiscard]] constexpr T normalize(T value) const noexcept requires std::floating_point<T> {
        return invLerp(minimum_, maximum_, value);
    }

    /// 取归一化位置 t 处的值。
    [[nodiscard]] constexpr T valueAt(T t) const noexcept requires std::floating_point<T> {
        return math::lerp(minimum_, maximum_, t);
    }

    /// 向外扩展以包含 value。
    constexpr void expandToInclude(T value) noexcept {
        if (value < minimum_) {
            minimum_ = value;
        }
        if (value > maximum_) {
            maximum_ = value;
        }
    }

    /// 向外扩展以包含另一区间，空区间不参与合并。
    constexpr void expandToInclude(const Range& other) noexcept {
        if (other.empty()) {
            return;
        }
        expandToInclude(other.minimum_);
        expandToInclude(other.maximum_);
    }

    /// 两侧各留出 padding。
    [[nodiscard]] constexpr Range expanded(T padding) const noexcept {
        return Range{static_cast<T>(minimum_ - padding), static_cast<T>(maximum_ + padding)};
    }

    /// 最小外接区间。
    [[nodiscard]] constexpr Range unitedWith(const Range& other) const noexcept {
        if (empty()) {
            return other;
        }
        if (other.empty()) {
            return *this;
        }
        return Range{minimum_ < other.minimum_ ? minimum_ : other.minimum_,
                     maximum_ > other.maximum_ ? maximum_ : other.maximum_};
    }

    /// 交集，无交集时返回空区间（maximum < minimum）。
    [[nodiscard]] constexpr Range intersectedWith(const Range& other) const noexcept {
        const T low = minimum_ > other.minimum_ ? minimum_ : other.minimum_;
        const T high = maximum_ < other.maximum_ ? maximum_ : other.maximum_;
        return Range{low, high};
    }

private:
    T minimum_{};
    T maximum_{};
};

using RangeF = Range<float>;
using RangeD = Range<double>;

}  // namespace kiriyama::math
