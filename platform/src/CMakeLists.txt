set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------- 文本核心
# 字体度量 + 排版。不依赖 SDL，可以在 node 里直接跑单元测试。

add_library(kiriyama_text STATIC
    src/Font.cc
    src/TextLayout.cc
)
target_include_directories(kiriyama_text PUBLIC include)
target_link_libraries(kiriyama_text PUBLIC kiriyama_core)

# ---------------------------------------------------------------- 场景行为
# 动画 / 拖拽：驱动节点自身的 transform 与 style，不碰 SDL。
# 和 TEXT 一样属于"纯逻辑"，因此能在 node 里直接跑单元测试。

add_library(kiriyama_scene STATIC
    src/Animation.cc
    src/Drag.cc
    src/PieChart.cc
    src/SparkBar.cc
    src/TableWidget.cc
    src/TooltipTarget.cc
    src/Widget.cc
)
target_include_directories(kiriyama_scene PUBLIC include)
target_link_libraries(kiriyama_scene PUBLIC kiriyama_core)

# ---------------------------------------------------------------- SDL 渲染后端

add_library(kiriyama_render STATIC
    src/App.cc
    src/Painter.cc
    src/SceneDraw.cc
    src/SdlColor.cc
    src/SparkBarRenderer.cc
    src/TextRenderer.cc
    src/TooltipHost.cc
    src/TooltipRenderer.cc
    src/TtfFont.cc
)
target_include_directories(kiriyama_render PUBLIC include)
target_link_libraries(kiriyama_render
    PUBLIC kiriyama_text kiriyama_scene kiriyama_core SDL3::SDL3 SDL3_ttf::SDL3_ttf)

# ---------------------------------------------------------------- 宿主
#
# 这里没有宿主：宿主是平台的事。
#   · 原来 app/TextDemo.cc、app/TableDemo.cc 各是一个可执行文件，
#     分别产 index.html / table.html；pie 和 sparkbar 又在 platform/web，
#     于是 web/ 里堆了四套 js/wasm/data（每套都带同一份字体，1.6MB）。
#   · 现在统一由 platform/web/CanvasHost.cc 一个宿主承载，页面用
#     kiriyama_show_component() 决定挂哪些组件，产物只有一个 kiriyama_canvas。

add_subdirectory(tests)