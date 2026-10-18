#pragma once

// 画笔：SDL 后端里唯一直接调 SDL 图元 API 的地方。
//
// 之前调用方里散着 setColor / fillRect / strokeRect 几个自由函数，那是胶水，
// 不是 API —— 谁来画都要抄一遍，还会各自把颜色量化逻辑写错。现在收敛到这里：
//
//   1) 状态式链式（推荐，颜色 + 透明度会一直生效到下次改）：
//        painter.color(kInk).opacity(0.4).fillRect(card);
//        painter.color(kBorder).strokeWidth(2.0).strokeRect(card).strokeWidth(1.0);
//
//   2) 每个 setter 都返回 *this，所以配置和绘制可以串成一句；
//      画完想恢复默认就 reset()。
//
// 坐标：全部是逻辑坐标（和 SDL 的 logical presentation 同一空间），
// 单位是逻辑像素；描边宽度也按逻辑像素算。
//
// 分工：Painter 只画"图元"，不认识 SceneNode。场景树的遍历在 SceneRenderer /
// TextRenderer 里做，它们拿着 Painter 往画布上落笔。

#include <span>

#include <SDL3/SDL.h>

#include "kiriyama/core/Color.h"
#include "kiriyama/core/Geometry.h"

namespace kiriyama::render {

class Painter {
public:
    explicit Painter(SDL_Renderer* renderer) noexcept;

    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return renderer_; }
    [[nodiscard]] bool valid() const noexcept { return renderer_ != nullptr; }

    // ------------------------------------------------------------ 状态
    // 全部返回 *this，可以链式串下去。

    /// 设置当前颜色。alpha 取自 color 自身，再乘 opacity()。
    Painter& color(const core::Color& value) noexcept;
    /// 一步到位：颜色 + 透明度，省一次调用。
    Painter& color(const core::Color& value, double alpha) noexcept;
    /// 额外透明度，0~1，乘在当前颜色的 alpha 上（用于 hover 淡化、disabled 等）。
    Painter& opacity(double value) noexcept;
    /// 描边宽度（逻辑像素）。1 表示最细的线。
    Painter& strokeWidth(double pixels) noexcept;
    /// 回到默认状态：透明黑、opacity = 1、线宽 = 1。
    Painter& reset() noexcept;

    [[nodiscard]] const core::Color& color() const noexcept { return color_; }
    [[nodiscard]] double opacity() const noexcept { return opacity_; }
    [[nodiscard]] double strokeWidth() const noexcept { return strokeWidth_; }

    // ------------------------------------------------------------ 绘制
    // 都用当前状态；返回值同样是 *this，画完还能接着画。

    /// 用当前颜色铺满整个渲染目标。
    Painter& clear();

    Painter& fillRect(const core::RectD& rect);
    Painter& strokeRect(const core::RectD& rect);

    /// 圆角矩形。radius 会被夹到短边的一半，0 等价于 fillRect。
    Painter& fillRoundRect(const core::RectD& rect, double radius);
    Painter& strokeRoundRect(const core::RectD& rect, double radius);

    /// 线段。strokeWidth > 1 时按法线方向并排铺多条。
    Painter& line(const core::Vec2d& from, const core::Vec2d& to);
    /// 折线；closed 为 true 时补上收尾段（等价于描边多边形）。
    Painter& polyline(std::span<const core::Vec2d> points, bool closed = false);

    /// 任意简单多边形填充（凸/凹都行，用三角扇 + 几何管线，不走扫描线）。
    Painter& fillPolygon(std::span<const core::Vec2d> points);

    Painter& fillEllipse(const core::RectD& box);
    Painter& strokeEllipse(const core::RectD& box);
    Painter& fillCircle(const core::Vec2d& center, double radius);
    Painter& strokeCircle(const core::Vec2d& center, double radius);

private:
    void applyColor() noexcept;
    [[nodiscard]] SDL_FColor currentFColor() const noexcept;
    void strokePath(std::span<const core::Vec2d> points, bool closed);
    void drawSegment(const core::Vec2d& from, const core::Vec2d& to);

    SDL_Renderer* renderer_{nullptr};
    core::Color color_{};
    double opacity_{1.0};
    double strokeWidth_{1.0};
};

}  // namespace kiriyama::render
