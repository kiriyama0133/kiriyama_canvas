#include "kiriyama/render/Painter.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "kiriyama/render/SdlColor.h"

namespace kiriyama::render {
namespace {

[[nodiscard]] constexpr double clamp01(double value) noexcept {
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

[[nodiscard]] SDL_FRect toFRect(const core::RectD& rect) noexcept {
    const core::RectD n = rect.normalized();
    return SDL_FRect{static_cast<float>(n.x), static_cast<float>(n.y),
                     static_cast<float>(n.width), static_cast<float>(n.height)};
}

[[nodiscard]] int segmentCount(double radius) noexcept {
    // 半径越大段数越多，但封顶——超过 180 段肉眼已经分辨不出，白烧顶点。
    const int wanted = static_cast<int>(std::ceil(radius * 0.7));
    return std::clamp(wanted, 24, 180);
}

/// 圆角矩形的轮廓点，顺时针（屏幕坐标 y 向下）。凸多边形，可直接喂三角扇。
[[nodiscard]] std::vector<core::Vec2d> roundRectPath(const core::RectD& rect,
                                                     double radius) {
    const core::RectD r = rect.normalized();
    const double limit = std::min(r.width, r.height) * 0.5;
    const double rad = std::clamp(radius, 0.0, limit);

    if (rad <= 0.0) {
        return {{r.left(), r.top()},
                {r.right(), r.top()},
                {r.right(), r.bottom()},
                {r.left(), r.bottom()}};
    }

    const int steps = std::clamp(static_cast<int>(std::ceil(rad * 0.7)), 4, 24);
    constexpr double kHalfPi = 1.5707963267948966;

    std::vector<core::Vec2d> path;
    path.reserve(static_cast<std::size_t>(steps + 1) * 4);

    // 四个角依次：右上 → 右下 → 左下 → 左上，每段扫 90°。
    const core::Vec2d centers[4] = {{r.right() - rad, r.top() + rad},
                                    {r.right() - rad, r.bottom() - rad},
                                    {r.left() + rad, r.bottom() - rad},
                                    {r.left() + rad, r.top() + rad}};
    const double starts[4] = {-kHalfPi, 0.0, kHalfPi, kHalfPi * 2.0};

    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= steps; ++i) {
            const double angle = starts[corner] + kHalfPi * (static_cast<double>(i) / steps);
            path.push_back({centers[corner].x + std::cos(angle) * rad,
                            centers[corner].y + std::sin(angle) * rad});
        }
    }
    return path;
}

/// 椭圆 / 圆的轮廓点。
[[nodiscard]] std::vector<core::Vec2d> ellipsePath(const core::RectD& box) {
    const core::RectD r = box.normalized();
    const core::Vec2d center = r.center();
    const double rx = r.width * 0.5;
    const double ry = r.height * 0.5;
    const int steps = segmentCount(std::max(rx, ry));
    constexpr double kTau = 6.283185307179586;

    std::vector<core::Vec2d> path;
    path.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        const double angle = kTau * (static_cast<double>(i) / steps);
        path.push_back({center.x + std::cos(angle) * rx, center.y + std::sin(angle) * ry});
    }
    return path;
}

}  // namespace

Painter::Painter(SDL_Renderer* renderer) noexcept : renderer_(renderer) {
    if (renderer_) {
        // 图元颜色带 alpha，后端必须开混合，否则 opacity 形同虚设。
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }
}

// ---------------------------------------------------------------- 状态

Painter& Painter::color(const core::Color& value) noexcept {
    color_ = value;
    return *this;
}

Painter& Painter::color(const core::Color& value, double alpha) noexcept {
    color_ = value;
    opacity_ = clamp01(alpha);
    return *this;
}

Painter& Painter::opacity(double value) noexcept {
    opacity_ = clamp01(value);
    return *this;
}

Painter& Painter::strokeWidth(double pixels) noexcept {
    strokeWidth_ = pixels > 0.0 ? pixels : 1.0;
    return *this;
}

Painter& Painter::reset() noexcept {
    color_ = core::Color{};
    opacity_ = 1.0;
    strokeWidth_ = 1.0;
    return *this;
}

// ---------------------------------------------------------------- 绘制

Painter& Painter::clear() {
    if (!renderer_) {
        return *this;
    }
    applyColor();
    SDL_RenderClear(renderer_);
    return *this;
}

Painter& Painter::fillRect(const core::RectD& rect) {
    if (!renderer_ || rect.empty() || opacity_ <= 0.0) {
        return *this;
    }
    applyColor();
    const SDL_FRect r = toFRect(rect);
    SDL_RenderFillRect(renderer_, &r);
    return *this;
}

Painter& Painter::strokeRect(const core::RectD& rect) {
    if (!renderer_ || rect.empty() || opacity_ <= 0.0) {
        return *this;
    }
    applyColor();

    const SDL_FRect r = toFRect(rect);
    const int layers = std::max(1, static_cast<int>(std::lround(strokeWidth_)));
    // 以矩形边界为中心线向内外各铺一层，宽度 2 时就是 2px 的框。
    for (int i = 0; i < layers; ++i) {
        const float offset = static_cast<float>(i) - static_cast<float>(layers - 1) * 0.5f;
        const SDL_FRect ring{r.x + offset, r.y + offset, r.w - offset * 2.0f, r.h - offset * 2.0f};
        if (ring.w <= 0.0f || ring.h <= 0.0f) {
            continue;
        }
        SDL_RenderRect(renderer_, &ring);
    }
    return *this;
}

Painter& Painter::fillRoundRect(const core::RectD& rect, double radius) {
    if (!renderer_ || rect.empty() || opacity_ <= 0.0) {
        return *this;
    }
    const std::vector<core::Vec2d> path = roundRectPath(rect, radius);
    return fillPolygon(path);
}

Painter& Painter::strokeRoundRect(const core::RectD& rect, double radius) {
    if (!renderer_ || rect.empty() || opacity_ <= 0.0) {
        return *this;
    }
    const std::vector<core::Vec2d> path = roundRectPath(rect, radius);
    strokePath(path, true);
    return *this;
}

Painter& Painter::line(const core::Vec2d& from, const core::Vec2d& to) {
    if (!renderer_ || opacity_ <= 0.0) {
        return *this;
    }
    applyColor();
    drawSegment(from, to);
    return *this;
}

Painter& Painter::polyline(std::span<const core::Vec2d> points, bool closed) {
    if (!renderer_ || points.size() < 2 || opacity_ <= 0.0) {
        return *this;
    }
    applyColor();
    strokePath(points, closed);
    return *this;
}

Painter& Painter::fillPolygon(std::span<const core::Vec2d> points) {
    if (!renderer_ || points.size() < 3 || opacity_ <= 0.0) {
        return *this;
    }

    const SDL_FColor tint = currentFColor();
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(points.size());
    for (const core::Vec2d& point : points) {
        vertices.push_back(SDL_Vertex{SDL_FPoint{static_cast<float>(point.x),
                                                 static_cast<float>(point.y)},
                                      tint, SDL_FPoint{0.0f, 0.0f}});
    }

    // 三角扇：对凸多边形一步到位；凹多边形在 GPU 上会画歪，但圆角矩形 / 椭圆
    // 全是凸的，图表里用到的多边形（面积填充、扇区）也基本都是凸的。
    std::vector<int> indices;
    indices.reserve((points.size() - 2) * 3);
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<int>(i));
        indices.push_back(static_cast<int>(i + 1));
    }

    if (SDL_RenderGeometry(renderer_, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                           indices.data(), static_cast<int>(indices.size()))) {
        return *this;
    }

    // 软件渲染器不支持几何管线，退回扫描线（偶奇规则，凹多边形也对）。
    applyColor();
    double top = points[0].y;
    double bottom = points[0].y;
    for (const core::Vec2d& point : points) {
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }

    std::vector<double> crossings;
    for (double y = std::floor(top) + 0.5; y < bottom; y += 1.0) {
        crossings.clear();
        for (std::size_t i = 0; i < points.size(); ++i) {
            const core::Vec2d& a = points[i];
            const core::Vec2d& b = points[(i + 1) % points.size()];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                crossings.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
            }
        }
        std::sort(crossings.begin(), crossings.end());
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            SDL_RenderLine(renderer_, static_cast<float>(crossings[i]), static_cast<float>(y),
                           static_cast<float>(crossings[i + 1]), static_cast<float>(y));
        }
    }
    return *this;
}

Painter& Painter::fillEllipse(const core::RectD& box) {
    if (!renderer_ || box.empty() || opacity_ <= 0.0) {
        return *this;
    }
    const std::vector<core::Vec2d> path = ellipsePath(box);
    return fillPolygon(path);
}

Painter& Painter::strokeEllipse(const core::RectD& box) {
    if (!renderer_ || box.empty() || opacity_ <= 0.0) {
        return *this;
    }
    applyColor();
    const std::vector<core::Vec2d> path = ellipsePath(box);
    strokePath(path, true);
    return *this;
}

Painter& Painter::fillCircle(const core::Vec2d& center, double radius) {
    if (radius <= 0.0) {
        return *this;
    }
    return fillEllipse(core::RectD{center.x - radius, center.y - radius, radius * 2.0,
                                   radius * 2.0});
}

Painter& Painter::strokeCircle(const core::Vec2d& center, double radius) {
    if (radius <= 0.0) {
        return *this;
    }
    return strokeEllipse(core::RectD{center.x - radius, center.y - radius, radius * 2.0,
                                     radius * 2.0});
}

// ---------------------------------------------------------------- 内部

void Painter::applyColor() noexcept {
    const core::Rgba8 bytes = color_.withOpacity(opacity_).toBytes();
    SDL_SetRenderDrawColor(renderer_, bytes.r, bytes.g, bytes.b, bytes.a);
}

SDL_FColor Painter::currentFColor() const noexcept {
    const double alpha = static_cast<double>(color_.a) * opacity_;
    return SDL_FColor{color_.r, color_.g, color_.b, static_cast<float>(alpha)};
}

void Painter::strokePath(std::span<const core::Vec2d> points, bool closed) {
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        drawSegment(points[i], points[i + 1]);
    }
    if (closed && points.size() > 2) {
        drawSegment(points.back(), points.front());
    }
}

void Painter::drawSegment(const core::Vec2d& from, const core::Vec2d& to) {
    const int layers = std::max(1, static_cast<int>(std::lround(strokeWidth_)));
    if (layers == 1) {
        SDL_RenderLine(renderer_, static_cast<float>(from.x), static_cast<float>(from.y),
                       static_cast<float>(to.x), static_cast<float>(to.y));
        return;
    }

    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double length = std::hypot(dx, dy);
    if (length < 1e-9) {
        return;
    }
    // 法线方向并排铺线，让粗线看起来是一条带子而不是一串珠子。
    const double nx = -dy / length;
    const double ny = dx / length;
    for (int i = 0; i < layers; ++i) {
        const double offset = static_cast<double>(i) - static_cast<double>(layers - 1) * 0.5;
        SDL_RenderLine(renderer_, static_cast<float>(from.x + nx * offset),
                       static_cast<float>(from.y + ny * offset),
                       static_cast<float>(to.x + nx * offset),
                       static_cast<float>(to.y + ny * offset));
    }
}

}  // namespace kiriyama::render
