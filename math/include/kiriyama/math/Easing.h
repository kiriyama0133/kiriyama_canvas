#pragma once

// 缓动曲线：把线性的 t ∈ [0,1] 映射成"有节奏"的进度。
//
// 只提供与具体属性无关的一元函数，因此同一套曲线既能喂给位移动画，
// 也能喂给透明度 / 缩放 / 相机推进——调用方只需要 `ease(kind, p)`。
//
// 约定：
//   - 输入会被夹到 [0,1]；输出不夹（OutBack 这类要过冲，夹了就废了）。
//   - ease(Linear, 0) == 0、ease(*, 1) == 1，便于动画收尾时对齐目标值。
//   - 不做状态、不做时间，帧率无关由调用方按 dt 推进保证。

#include <cstdint>

#include "Interpolation.h"

namespace kiriyama::math {

enum class Ease : std::uint8_t {
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InSine,
    OutSine,
    InOutSine,
    /// Hermite 平滑阶跃，两端一阶导为 0。UI 里最稳的默认值。
    Smoothstep,
    /// 五次平滑，两端二阶导也为 0，长距离位移更"软"。
    Smootherstep,
    /// 末尾轻微过冲再回落，用于"弹出来"的强调。
    OutBack,
};

/// 求缓动值。t 夹到 [0,1]，返回值可能略超出 [0,1]（OutBack）。
[[nodiscard]] double ease(Ease kind, double t) noexcept;

}  // namespace kiriyama::math
