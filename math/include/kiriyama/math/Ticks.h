#pragma once

// 坐标轴刻度计算。
// 目标：不管数据量级怎么变，刻度都落在 1 / 2 / 5 × 10^n 上，标签干净可读。

#include <vector>

#include "MathUtils.h"
#include "Range.h"

namespace kiriyama::math {

/// 把 span 规整为 1、2、5、10 乘以 10 的整数次幂。
/// roundToNearest = true 取最接近的，false 取不小于 span 的。
[[nodiscard]] double niceNumber(double span, bool roundToNearest = true) noexcept;

/// 期望 targetCount 个刻度时，相邻刻度的间隔（已是 nice 数）。
[[nodiscard]] double niceStep(double span, int targetCount, bool roundToNearest = false) noexcept;

/// 生成覆盖 range 的刻度值，首尾会越过 range 边界各不超过一个 step。
/// range 退化时返回单个值。
[[nodiscard]] std::vector<double> linearTicks(const RangeD& range, int targetCount = 5);

/// 把 range 向外扩到最近的刻度值，让首尾刻度都能落在线端上。
/// includeZero = true 时强制把 0 纳入 domain（柱状图基本都需要）。
[[nodiscard]] RangeD niceDomain(const RangeD& range, int targetCount = 5,
                                bool includeZero = false);

/// 对数轴的刻度：range 内所有 10 的整数次幂。
/// range 完全非正时返回空。
[[nodiscard]] std::vector<double> logTicks(const RangeD& range);

}  // namespace kiriyama::math
