#pragma once

// 场景图总入口。实际定义按职责拆在下列头文件里，这里只做汇总：
//
//   Color.h       颜色（线性 RGBA）
//   NodeType.h    图元类型枚举
//   SceneStyle.h  样式：填充 / 描边 / 文字对齐
//   SceneNode.h   节点与树结构（含变换、包围盒、命中测试）
//
// 几何工具（变换后包围盒、多边形包含、到边框距离）在 Geometry.h，
// 查询与对账分别在 SceneQuery.h / SceneDiff.h。
//
// 拆分后调用方仍然只需要 #include "kiriyama/core/Scene.h"。

#include "Color.h"
#include "NodeType.h"
#include "SceneNode.h"
#include "SceneStyle.h"
