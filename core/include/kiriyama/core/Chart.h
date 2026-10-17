#pragma once

// core 模块总入口。
//
//   Data.h      数据模型（折线 / 柱状 Dataset，饼图 PieChartData）、空值与边界
//   Axis.h      坐标轴映射、对数轴、刻度生成与格式化
//   Layout.h    padding / 绘图区 / 双轴对齐 / 柱形与饼图几何
//   HitTest.h   碰撞与命中测试
//   Geometry.h  矩形、线段、圆弧、标签去重叠
//   VirtualScroll.h 长列表虚拟滚动（渲染范围 / 索引反查 / 选中追踪）
//   Scene.h     场景图总入口（汇总下面四个）
//   Color.h     颜色（线性 RGBA）
//   NodeType.h  图元类型枚举
//   SceneStyle.h 样式：填充 / 描边 / 文字对齐
//   SceneNode.h 节点与树结构（变换 / 包围盒 / 命中测试）
//   SceneQuery.h 树遍历、按 id/key 查找、包围盒、命中测试
//   SceneDiff.h 两棵树的增量对账（类 virtual DOM diff）
//   SceneBuilder.h 数据 + 布局 -> 描述树（含主题与图元组装）
//   Interaction.h hover / active / click 状态机（事件由 src 从 SDL 翻译进来）
//   Tooltip.h   气泡定位 + 显隐状态机（hover / focus 触发）
//   Table.h     表格数据模型 + 排序状态机（行置换 order[]、类型化比较）

#include "Axis.h"
#include "Data.h"
#include "Geometry.h"
#include "HitTest.h"
#include "Interaction.h"
#include "Layout.h"
#include "Scene.h"
#include "SceneBuilder.h"
#include "SceneDiff.h"
#include "SceneQuery.h"
#include "Table.h"
#include "Tooltip.h"
#include "VirtualScroll.h"
