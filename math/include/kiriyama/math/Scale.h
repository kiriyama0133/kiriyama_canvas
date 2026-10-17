#pragma once

// 数据域到像素域的映射。
// 图表所有坐标换算都集中在这里，渲染层不再自己做除法。

#include <cmath>
#include <limits>

#include "MathUtils.h"
#include "Range.h"

namespace kiriyama::math {

/// 线性标度：value -> position，两域均可反向（range 降序即 y 轴向下）。
class LinearScale {
public:
    constexpr LinearScale() noexcept : domain_{0.0, 1.0}, range_{0.0, 1.0} {}
    constexpr LinearScale(RangeD domain, RangeD range) noexcept : domain_(domain), range_(range) {}

    [[nodiscard]] constexpr const RangeD& domain() const noexcept { return domain_; }
    [[nodiscard]] constexpr const RangeD& range() const noexcept { return range_; }

    constexpr void setDomain(RangeD domain) noexcept { domain_ = domain; }
    constexpr void setRange(RangeD range) noexcept { range_ = range; }

    /// 每个数据单位占多少像素。domain 退化时为 0。
    [[nodiscard]] constexpr double scaleFactor() const noexcept {
        return safeDiv(range_.span(), domain_.span(), 0.0);
    }

    /// 映射到像素坐标。domain 退化时返回 range 起点。
    [[nodiscard]] constexpr double map(double value) const noexcept {
        if (domain_.degenerate()) {
            return range_.minimum();
        }
        return range_.minimum() + (value - domain_.minimum()) * scaleFactor();
    }

    /// 映射并把 value 夹在 domain 内，适合画超出可视区的数据。
    [[nodiscard]] constexpr double mapClamped(double value) const noexcept {
        return map(domain_.clamp(value));
    }

    /// 像素坐标反查数据值。
    [[nodiscard]] constexpr double invert(double position) const noexcept {
        if (range_.degenerate()) {
            return domain_.minimum();
        }
        const double ratio = (position - range_.minimum()) / range_.span();
        return domain_.minimum() + ratio * domain_.span();
    }

    /// 数据值在 domain 中的归一化位置 [0, 1]（不夹取）。
    [[nodiscard]] constexpr double normalize(double value) const noexcept {
        return domain_.normalize(value);
    }

    /// 归一化位置 t 对应的像素坐标。
    [[nodiscard]] constexpr double positionAt(double t) const noexcept {
        return range_.valueAt(t);
    }

    /// domain 与 range 都非退化才可用于绘制。
    [[nodiscard]] constexpr bool valid() const noexcept {
        return !domain_.degenerate() && !range_.degenerate();
    }

private:
    RangeD domain_;
    RangeD range_;
};

/// 对数标度（以 10 为底）：用于量级跨度大的数据，如人口、文件大小、价格倍数。
/// domain 必须为正数，非正值会被夹到 domain 下界，避免 log(NaN) 污染整条曲线。
class LogScale {
public:
    LogScale() noexcept : domain_{1.0, 10.0}, range_{0.0, 1.0} {}

    LogScale(RangeD domain, RangeD range) noexcept
        : domain_(sanitize(domain)), range_(range) {}

    [[nodiscard]] const RangeD& domain() const noexcept { return domain_; }
    [[nodiscard]] const RangeD& range() const noexcept { return range_; }

    void setDomain(RangeD domain) noexcept { domain_ = sanitize(domain); }
    void setRange(RangeD range) noexcept { range_ = range; }

    [[nodiscard]] double map(double value) const noexcept {
        // domain 已被 sanitize 保证下界 > 0，因此夹取后一定可以做 log10。
        const double clamped = domain_.clamp(value);
        if (domain_.degenerate()) {
            return range_.minimum();
        }
        const double ratio = (std::log10(clamped) - std::log10(domain_.minimum())) /
                             (std::log10(domain_.maximum()) - std::log10(domain_.minimum()));
        return range_.minimum() + ratio * range_.span();
    }

    [[nodiscard]] double invert(double position) const noexcept {
        if (range_.degenerate()) {
            return domain_.minimum();
        }
        const double ratio = (position - range_.minimum()) / range_.span();
        const double exponent = std::log10(domain_.minimum()) +
                                ratio * (std::log10(domain_.maximum()) -
                                         std::log10(domain_.minimum()));
        return std::pow(10.0, exponent);
    }

    [[nodiscard]] bool valid() const noexcept {
        return domain_.minimum() > 0.0 && !domain_.degenerate() && !range_.degenerate();
    }

private:
    /// 保证下界严格大于 0，否则 log10 无定义。
    [[nodiscard]] static RangeD sanitize(RangeD domain) noexcept {
        const double lowest = std::numeric_limits<double>::min();
        double low = domain.minimum();
        double high = domain.maximum();
        if (!(low > 0.0)) {
            low = lowest;
        }
        if (high < low) {
            high = low * 10.0;
        }
        return RangeD{low, high};
    }

    RangeD domain_;
    RangeD range_;
};

}  // namespace kiriyama::math
