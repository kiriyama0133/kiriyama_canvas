#include "kiriyama/math/Ticks.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace kiriyama::math {
namespace {

/// 刻度数量的硬上限，防止极端 domain 把内存打爆（wasm 环境里内存是死线）。
constexpr std::size_t kMaxTickCount = 4096;

/// 把 -0.0 和 1e-17 这类浮点残渣收成干净的 0。
[[nodiscard]] double cleanZero(double value, double step) noexcept {
  const double tolerance = std::abs(step) * 1e-9;
  if (std::abs(value) <= tolerance) {
    return 0.0;
  }
  return value;
}

} // namespace

double niceNumber(double span, bool roundToNearest) noexcept {
  if (!(span > 0.0) || !std::isfinite(span)) {
    return 1.0;
  }

  const double exponent = std::floor(std::log10(span));
  const double power = std::pow(10.0, exponent);
  const double fraction = span / power;

  double niceFraction = 10.0;
  if (roundToNearest) {
    if (fraction < 1.5) {
      niceFraction = 1.0;
    } else if (fraction < 3.0) {
      niceFraction = 2.0;
    } else if (fraction < 7.0) {
      niceFraction = 5.0;
    }
  } else {
    if (fraction <= 1.0) {
      niceFraction = 1.0;
    } else if (fraction <= 2.0) {
      niceFraction = 2.0;
    } else if (fraction <= 5.0) {
      niceFraction = 5.0;
    }
  }

  return niceFraction * power;
}

double niceStep(double span, int targetCount, bool roundToNearest) noexcept {
  const int count = targetCount > 0 ? targetCount : 1;
  return niceNumber(span / static_cast<double>(count), roundToNearest);
}

std::vector<double> linearTicks(const RangeD &range, int targetCount) {
  std::vector<double> ticks;
  if (range.empty()) {
    return ticks;
  }
  if (range.degenerate()) {
    ticks.push_back(range.minimum());
    return ticks;
  }

  const double step =
      niceStep(range.span(), targetCount, /*roundToNearest=*/false);
  if (!(step > 0.0) || !std::isfinite(step)) {
    ticks.push_back(range.minimum());
    return ticks;
  }

  const double firstIndex = std::floor(range.minimum() / step);
  const double lastIndex = std::ceil(range.maximum() / step);
  if (lastIndex - firstIndex > static_cast<double>(kMaxTickCount)) {
    return ticks;
  }

  ticks.reserve(static_cast<std::size_t>(lastIndex - firstIndex) + 1);
  for (double index = firstIndex; index <= lastIndex; index += 1.0) {
    ticks.push_back(cleanZero(index * step, step));
  }
  return ticks;
}

RangeD niceDomain(const RangeD &range, int targetCount, bool includeZero) {
  RangeD domain = range;
  if (includeZero) {
    domain.expandToInclude(0.0);
  }
  if (domain.empty()) {
    return domain;
  }

  if (domain.degenerate()) {
    // 单点数据也要给出一段可视范围，否则线画不出来。
    const double center = domain.minimum();
    const double padding = center == 0.0 ? 1.0 : std::abs(center) * 0.5;
    return RangeD{center - padding, center + padding};
  }

  const double step =
      niceStep(domain.span(), targetCount, /*roundToNearest=*/false);
  if (!(step > 0.0) || !std::isfinite(step)) {
    return domain;
  }
  return RangeD{std::floor(domain.minimum() / step) * step,
                std::ceil(domain.maximum() / step) * step};
}

std::vector<double> logTicks(const RangeD &range) {
  std::vector<double> ticks;
  if (!(range.maximum() > 0.0) || range.empty()) {
    return ticks;
  }

  const double lowest = range.minimum() > 0.0
                            ? range.minimum()
                            : std::numeric_limits<double>::min();
  const double firstExponent = std::ceil(std::log10(lowest));
  const double lastExponent = std::floor(std::log10(range.maximum()));
  if (lastExponent - firstExponent > static_cast<double>(kMaxTickCount)) {
    return ticks;
  }

  for (double exponent = firstExponent; exponent <= lastExponent;
       exponent += 1.0) {
    ticks.push_back(std::pow(10.0, exponent));
  }
  return ticks;
}

} // namespace kiriyama::math
