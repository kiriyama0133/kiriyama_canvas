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
    src/Widget.cc
)
target_include_directories(kiriyama_scene PUBLIC include)
target_link_libraries(kiriyama_scene PUBLIC kiriyama_core)

# ---------------------------------------------------------------- SDL 渲染后端

add_library(kiriyama_render STATIC
    src/App.cc
    src/Painter.cc
    src/SdlColor.cc
    src/TableWidget.cc
    src/TextRenderer.cc
    src/TooltipRenderer.cc
    src/TtfFont.cc
)
target_include_directories(kiriyama_render PUBLIC include)
target_link_libraries(kiriyama_render
    PUBLIC kiriyama_text kiriyama_scene kiriyama_core SDL3::SDL3 SDL3_ttf::SDL3_ttf)

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
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sASSERTIONS=1"
        "-sSTACK_SIZE=1048576"
    )
else()
    set_target_properties(kiriyama_text_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# ---------------------------------------------------------------- 表格演示
#
# 模型不写在 C++ 里：web/table.html 的 <script> 里有一份 JS 模型，运行时编码成
# "\x1f 分隔" 的行协议，通过 EMSCRIPTEN_KEEPALIVE 的 kiriyama_table_load 送进来。
# 换数据 / 换列 / 换格式化串都不用重新编译。

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
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sASSERTIONS=1"
        "-sSTACK_SIZE=1048576"
        # 页面用 Module.ccall('kiriyama_table_load', ..., ['string'], ...) 传模型。
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','stringToUTF8','UTF8ToString']"
    )
else()
    set_target_properties(kiriyama_table_demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

add_subdirectory(tests)