#include "kiriyama/render/SceneDraw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <SDL3/SDL.h>

#include "kiriyama/core/NodeType.h"
#include "kiriyama/math/Transform2D.h"

namespace kiriyama::render {

using core::NodeType;
using core::Paint;
using core::RectD;
using core::SceneNode;
using kiriyama::math::Affine2D;
using kiriyama::math::Vec2d;

namespace {

/// 矩形经仿射变换后的世界包围盒（几何只支持平移/缩放，四角取 min/max 足够）。
[[nodiscard]] RectD transformedRect(const Affine2D& world, const RectD& rect) noexcept {
    if (rect.empty()) {
        return {};
    }
    const std::array<Vec2d, 4> corners{
        Vec2d{rect.x, rect.y},
        Vec2d{rect.x + rect.width, rect.y},
        Vec2d{rect.x + rect.width, rect.y + rect.height},
        Vec2d{rect.x, rect.y + rect.height}};
    Vec2d min = world.apply(corners[0]);
    Vec2d max = min;
    for (std::size_t i = 1; i < corners.size(); ++i) {
        const Vec2d point = world.apply(corners[i]);
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
    }
    return RectD::FromLTRB(min.x, min.y, max.x, max.y);
}

[[nodiscard]] std::vector<Vec2d> transformedPoints(const Affine2D& world,
                                                   std::span<const Vec2d> points) {
    std::vector<Vec2d> out;
    out.reserve(points.size());
    for (const Vec2d& point : points) {
        out.push_back(world.apply(point));
    }
    return out;
}

/// 把节点声明的裁剪矩形落成 SDL 裁剪（世界坐标）。返回是否生效；
/// 退出子树时用 restore 恢复（没有外层裁剪就清掉）。
[[nodiscard]] bool pushClip(Painter& painter, const Affine2D& world, const RectD& localClip,
                            SDL_Rect& saved) {
    SDL_Renderer* renderer = painter.renderer();
    if (renderer == nullptr || localClip.empty()) {
        return false;
    }
    const RectD clip = transformedRect(world, localClip);
    if (clip.empty()) {
        return false;
    }
    SDL_GetRenderClipRect(renderer, &saved);
    const SDL_Rect next{static_cast<int>(std::lround(clip.x)),
                        static_cast<int>(std::lround(clip.y)),
                        static_cast<int>(std::lround(clip.width)),
                        static_cast<int>(std::lround(clip.height))};
    (void)SDL_SetRenderClipRect(renderer, &next);
    return true;
}

void restoreClip(Painter& painter, bool clipped, const SDL_Rect& saved) {
    if (!clipped) {
        return;
    }
    SDL_Renderer* renderer = painter.renderer();
    if (saved.w > 0 && saved.h > 0) {
        (void)SDL_SetRenderClipRect(renderer, &saved);
    } else {
        (void)SDL_SetRenderClipRect(renderer, nullptr);
    }
}

void drawNode(Painter& painter, const SceneNode& node, const Affine2D& parentWorld,
              double parentOpacity, const core::ChartTheme& theme,
              const std::vector<core::Color>* seriesPalette) {
    if (!node.visible()) {
        return;
    }
    const core::SceneStyle& style = node.style();
    const double effective = parentOpacity * style.opacity;
    if (effective <= 0.0) {
        return;  // 整棵子树都不可见
    }

    // 局部系列色板沿树继承：本节点声明了就用它的，最近祖先优先。
    const std::vector<core::Color>* palette = style.seriesPalette != nullptr
                                                  ? style.seriesPalette.get()
                                                  : seriesPalette;

    // token 在这里才变成真色：同一棵树，换主题 = 换解析结果，树不用动。
    const core::Color fill = style.fill.resolve(theme, palette);
    const core::Color stroke = style.stroke.resolve(theme, palette);

    // 祖先变换在这条递归里累积：plot 组的平移就是靠它落到每个图元上的。
    const Affine2D world = parentWorld * node.transform();

    const double savedOpacity = painter.opacity();
    painter.opacity(savedOpacity * effective);

    // 裁剪：描述树用 clipRect 表达"只露出这一块"（滚动视口）。
    SDL_Rect savedClip{};
    const bool clipped = pushClip(painter, world, node.clipRect(), savedClip);

    const std::span<const Vec2d> points = node.points();
    switch (node.type()) {
        case NodeType::Rect: {
            const RectD frame = transformedRect(world, node.frame());
            if (!frame.empty()) {
                if (fill.visible()) {
                    painter.color(fill).fillRoundRect(frame, style.cornerRadius);
                }
                if (stroke.visible() && style.strokeWidth > 0.0) {
                    painter.color(stroke).strokeWidth(style.strokeWidth)
                        .strokeRoundRect(frame, style.cornerRadius);
                }
            }
            break;
        }
        case NodeType::Ellipse: {
            const RectD frame = transformedRect(world, node.frame());
            if (!frame.empty()) {
                if (fill.visible()) {
                    painter.color(fill).fillEllipse(frame);
                }
                if (stroke.visible() && style.strokeWidth > 0.0) {
                    painter.color(stroke).strokeWidth(style.strokeWidth)
                        .strokeEllipse(frame);
                }
            }
            break;
        }
        case NodeType::Polygon: {
            if (points.size() >= 3) {
                if (fill.visible()) {
                    painter.color(fill).fillPolygon(transformedPoints(world, points));
                }
                if (stroke.visible() && style.strokeWidth > 0.0) {
                    painter.color(stroke).strokeWidth(style.strokeWidth)
                        .polyline(transformedPoints(world, points), true);
                }
            }
            break;
        }
        case NodeType::Polyline: {
            if (points.size() >= 2 && stroke.visible() && style.strokeWidth > 0.0) {
                painter.color(stroke).strokeWidth(style.strokeWidth)
                    .polyline(transformedPoints(world, points), false);
            }
            break;
        }
        case NodeType::Line: {
            if (points.size() >= 2 && stroke.visible() && style.strokeWidth > 0.0) {
                painter.color(stroke).strokeWidth(style.strokeWidth)
                    .line(world.apply(points[0]), world.apply(points[1]));
            }
            break;
        }
        case NodeType::Group:
        case NodeType::Text:
            break;  // Group 无自身图元；Text 归 SdlTextRenderer::drawScene
    }

    for (const auto& child : node.children()) {
        if (child != nullptr) {
            drawNode(painter, *child, world, effective, theme, palette);
        }
    }

    restoreClip(painter, clipped, savedClip);
    painter.opacity(savedOpacity);
}

}  // namespace

void drawSceneGeometry(Painter& painter, const core::SceneNode& root,
                       const core::ChartTheme& theme) {
    drawNode(painter, root, Affine2D::Identity(), 1.0, theme, nullptr);
}

}  // namespace kiriyama::render
