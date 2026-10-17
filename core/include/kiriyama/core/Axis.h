#pragma once

// 坐标轴：数据域 <-> 像素域的映射，以及刻度值的生成与格式化。
//
// 关键约定：
//   - 线性轴支持 range 反向（y 轴向下时 range 降序），map/invert 自动处理。
//   - 对数轴只在 domain > 0 时有意义，构造时会把非正下界抬到正数区间。
//   - 刻度值落在 1 / 2 / 5 × 10^n 上；对数轴落在 10 的整数次幂上。

#include <string>
#include <vector>

#include "kiriyama/math/Range.h"
#include "kiriyama/math/Scale.h"

namespace kiriyama::core {

using math::RangeD;

enum class ScaleType {
    Linear,
    Log,
};

enum class AxisPosition {
    Bottom,
    Top,
    Left,
    Right,
};

/// 屏幕坐标的 y 轴方向。
/// Down（默认）= 原点在左上角，值越大像素 y 越大（SDL / Canvas 习惯）。
/// Up = 原点在左下角，值越大像素 y 越小（数学 / 统计图习惯）。
enum class YDirection {
    Up,
    Down,
};

// ---------------------------------------------------------------- 刻度格式化

struct TickFormat {
    /// < 0 表示按 step 自动推导小数位。
    int maxDecimals{-1};
    /// 1e3 以上用 k / M / B / T 后缀，避免标签撑爆左边距。
    bool compact{true};
    /// 非紧凑模式下是否加千分位逗号。
    bool grouping{true};

    [[nodiscard]] std::string format(double value, double step) const;
};

// ---------------------------------------------------------------- 轴

class Axis {
public:
    Axis() noexcept = default;
    Axis(ScaleType type, RangeD domain, RangeD pixelRange) noexcept;

    [[nodiscard]] ScaleType type() const noexcept { return type_; }
    [[nodiscard]] const RangeD& domain() const noexcept { return domain_; }
    [[nodiscard]] const RangeD& pixelRange() const noexcept { return pixelRange_; }

    void setType(ScaleType type) noexcept;
    void setDomain(RangeD domain) noexcept;
    void setPixelRange(RangeD pixelRange) noexcept;

    /// 数据值 -> 像素坐标。domain 退化时返回像素区间起点。
    [[nodiscard]] double map(double value) const noexcept;

    /// 先夹到 domain 再映射，适合画超出可视区的数据。
    [[nodiscard]] double mapClamped(double value) const noexcept;

    /// 像素坐标 -> 数据值。
    [[nodiscard]] double invert(double pixel) const noexcept;

    [[nodiscard]] bool valid() const noexcept;

    /// 按当前 domain 生成一批刻度值，数量接近 targetCount。
    [[nodiscard]] std::vector<double> tickValues(int targetCount = 5) const;

private:
    void rebuild() noexcept;

    ScaleType type_{ScaleType::Linear};
    RangeD domain_{0.0, 1.0};
    RangeD pixelRange_{0.0, 1.0};
    math::LinearScale linear_{};
    math::LogScale log_{};
};

// ---------------------------------------------------------------- 刻度工具

/// 对数轴的数据域：向外扩到整十的幂，保证 domain > 0。
/// 传入 0 或负数时，以下界的 1/1000 兜底（跨度约三个数量级）。
[[nodiscard]] RangeD niceLogDomain(RangeD range) noexcept;

/// 对数刻度：10 的整数次幂，按 span 自动跳级，保证不超过约 targetCount 个。
[[nodiscard]] std::vector<double> logTickValues(RangeD domain, int targetCount = 5);

/// 把 domain 等分成 intervals 段，返回 intervals + 1 个刻度值（首尾精确等于 domain 端点）。
[[nodiscard]] std::vector<double> evenTickValues(RangeD domain, int intervals);

}  // namespace kiriyama::core
