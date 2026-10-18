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
    src/PieSlice.cc
    src/TooltipTarget.cc
    src/Widget.cc
)
target_include_directories(kiriyama_scene PUBLIC include)
target_link_libraries(kiriyama_scene PUBLIC kiriyama_core)

# ---------------------------------------------------------------- SDL 渲染后端

add_library(kiriyama_render STATIC
    src/App.cc
    src/Painter.cc
    src/PieChartRenderer.cc
    src/SdlColor.cc
    src/TableWidget.cc
    src/TextRenderer.cc
    src/TooltipHost.cc
    src/TooltipRenderer.cc
    src/TtfFont.cc
)
target_include_directories(kiriyama_render PUBLIC include)
target_link_libraries(kiriyama_render
    PUBLIC kiriyama_text kiriyama_scene kiriyama_core SDL3::SDL3 SDL3_ttf::SDL3_ttf)

# ---------------------------------------------------------------- wasm 公共标志
#
# 体积的大头是**构建类型**，不是代码量：Debug 会把 DWARF 调试信息和
# -sASSERTIONS 一起塞进产物，同一份代码 11 MB；Release 只有 1.6 MB。
# 所以两套标志分开，Debug 用来查错，Release 才是能发到网上的那一份。

if(EMSCRIPTEN)
    set(KIRIYAMA_WASM_COMMON
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sSTACK_SIZE=1048576"
        "-sENVIRONMENT=web"     # 去掉 node/shell 分支的胶水
        "--closure=1"           # 压缩 JS 胶水（wasm 不受影响）
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # -g1 保留空白与缩进：调试产物里的 glue JS 是能读的格式，
        # Release 依旧 closure 压成一行（部署用，体积优先）。
        list(APPEND KIRIYAMA_WASM_COMMON "-sASSERTIONS=1" "-g1" "-sMINIFY_HTML=0")
    else()
        list(APPEND KIRIYAMA_WASM_COMMON
            "-sASSERTIONS=0"    # 关掉运行时断言
            "-g0"               # 显式拒绝调试信息
            "-sMALLOC=emmalloc" # 比 dlmalloc 小，够用
        )
    endif()
endif()

# ---------------------------------------------------------------- 演示程序

add_executable(kiriyama_text_demo app/TextDemo.cc)
target_link_libraries(kiriyama_text_demo PRIVATE kiriyama_render kiriyama_fonts SDL3::SDL3)

if(EMSCRIPTEN)
    set_target_properties(kiriyama_text_demo PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web"
    )
    # 字体走 wasm 虚拟文件系统：只把 -DKIRIYAMA_FONTS 点名的文件 preload 到
    # /public/<文件名>，运行时用 "public/xxx.ttf" 就能同步打开。
    kiriyama_preload_fonts(kiriyama_text_demo)

    target_link_options(kiriyama_text_demo PRIVATE
        "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html"
        ${KIRIYAMA_WASM_COMMON}
    )
else()
    set_target_properties(kiriyama_text_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# ---------------------------------------------------------------- 表格演示
#
# 模型不写在 C++ 里：web/table.html 的 <script> 里挂一份 JS 模型，
# web/entry.js（--post-js 拼进 table.js）负责编码和调用 kiriyama_table_load。
# 页面只碰 { columns, rows }，不碰协议；换数据不用重新编译。

add_executable(kiriyama_table_demo app/TableDemo.cc)
target_link_libraries(kiriyama_table_demo PRIVATE kiriyama_render kiriyama_fonts SDL3::SDL3)

if(EMSCRIPTEN)
    set_target_properties(kiriyama_table_demo PROPERTIES
        OUTPUT_NAME "table"
        SUFFIX ".html"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web"
    )
    kiriyama_preload_fonts(kiriyama_table_demo)

    target_link_options(kiriyama_table_demo PRIVATE
        "--shell-file=${CMAKE_SOURCE_DIR}/web/table_shell.html"
        "--post-js=${CMAKE_SOURCE_DIR}/web/entry.js"
        ${KIRIYAMA_WASM_COMMON}
        # entry.js 里只用到 ccall，别的运行时方法一律不导出。
        "-sEXPORTED_RUNTIME_METHODS=['ccall']"
    )
else()
    set_target_properties(kiriyama_table_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# ---------------------------------------------------------------- 饼图演示
#
# 和表格共用 web/entry.js 胶水（--post-js 拼进 pie.js）：页面把
# { slices: [{label, value}...] } 挂到 window.KIRIYAMA_PIE_MODEL 上即可，
# 运行时 Module.loadPie(model) 换数据，不用重新编译。

add_executable(kiriyama_pie_demo app/PieDemo.cc)
target_link_libraries(kiriyama_pie_demo PRIVATE kiriyama_render kiriyama_fonts SDL3::SDL3)

if(EMSCRIPTEN)
    # 只产 pie.js / pie.wasm / pie.data——交付页是 web/pie_shell.html 自己
    # （<script src="pie.js"> 引产物，model / options 都写在页面里），
    # 不用 emscripten 的 shell-file 模板：那样源页打不开、产物页没 model，两头不是。
    set_target_properties(kiriyama_pie_demo PROPERTIES
        OUTPUT_NAME "pie"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web"
    )
    kiriyama_preload_fonts(kiriyama_pie_demo)

    target_link_options(kiriyama_pie_demo PRIVATE
        "--post-js=${CMAKE_SOURCE_DIR}/web/entry.js"
        ${KIRIYAMA_WASM_COMMON}
        "-sEXPORTED_RUNTIME_METHODS=['ccall']"
    )
else()
    set_target_properties(kiriyama_pie_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

add_subdirectory(tests)