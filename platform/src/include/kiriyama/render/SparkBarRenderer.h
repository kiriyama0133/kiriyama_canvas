#pragma once

// SparkStackBar 渲染器：把 layout 里的矩形画出来。
//
//   scene::SparkBar        数据指针 + 布局缓存
//   本类                    只落笔：段矩形（顶部圆角）、tag 文字
//
// 坐标：layout 是组件本地坐标（frame 左上角为原点），这里加上 frame.origin 再画；
// 和 PieChartRenderer 一样假定组件没有祖先变换。

#include <string>

#include "kiriyama/render/Font.h"
#include "kiriyama/render/Painter.h"
#include "kiriyama/scene/SparkBar.h"

namespace kiriyama::render {

class SdlTextRenderer;

class SparkBarRenderer {
public:
    SparkBarRenderer(Painter& painter, SdlTextRenderer& text);

    SparkBarRenderer(const SparkBarRenderer&) = delete;
    SparkBarRenderer& operator=(const SparkBarRenderer&) = delete;

    /// 画整组柱 + tag。放在 tickTree 之后调用。
    void draw(const scene::SparkBar& bar);

private:
    Painter& painter_;
    SdlTextRenderer& text_;
};

}  // namespace kiriyama::render
