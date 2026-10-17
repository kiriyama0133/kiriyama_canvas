#pragma once

// 图元类型。节点只声明"自己是什么形状"，具体几何存在 SceneNode 的
// frame / points 里，绘制交给渲染层。

#include <cstdint>

namespace kiriyama::core {

enum class NodeType : std::uint8_t {
    Group,     ///< 只做分组 / 变换，自身不画
    Rect,      ///< frame 内接矩形，支持 cornerRadius
    Ellipse,   ///< frame 内接椭圆（正方形 frame 即圆）
    Line,      ///< points[0] -> points[1]
    Polyline,  ///< points 顺序连接，不闭合
    Polygon,   ///< points 顺序连接并闭合，可填充
    Text,      ///< text + frame 作为排版盒
};

/// 稳定的英文名，用于日志、调试输出与 wasm 侧的类型判定。
[[nodiscard]] const char* toString(NodeType type) noexcept;

}  // namespace kiriyama::core
