#pragma once

// 描述树的通用几何绘制：Rect / Ellipse / Line / Polyline / Polygon。
//
// 图表组件把 model 变成描述树、reconcileScene 维护成渲染树之后，
// 渲染层只需要这一遍通用遍历——不再按图表类型各写一个 Renderer。
// Text 不归这里：字形资源与排版在 SdlTextRenderer，用它自己的
// drawScene(root) 画同一棵树里的文本节点。
//
// 样式约定（core::SceneStyle）：
//   fill / stroke          Paint（语义 token 或直接色），解析用传入的当前主题
//   cornerRadius           仅 Rect
//   opacity                逐层相乘（和 SdlTextRenderer::drawScene 一致）
//   visible == false       整棵子树跳过
//   seriesPalette          局部系列色板，沿树继承（最近祖先优先），
//                          替换主题里的全局 series 色板

#include "kiriyama/core/SceneNode.h"
#include "kiriyama/core/Theme.h"
#include "kiriyama/render/Painter.h"

namespace kiriyama::render {

/// 遍历渲染树的几何图元并上屏。theme 是当前页面主题（token 的解析来源）。
void drawSceneGeometry(Painter& painter, const core::SceneNode& root,
                       const core::ChartTheme& theme);

}  // namespace kiriyama::render
