cmake_minimum_required(VERSION 3.20)
project(kiriyama_canvas
        VERSION 1.0
        DESCRIPTION "canvas charts based in wasm"
        LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# clangd / IntelliSense 的编译数据库。每个文件在哪、带什么 -I / -std / 宏，
# 编辑器直接读 build/compile_commands.json——关了它，clangd 就靠猜，
# 新加的文件一律 "无法打开源文件 kiriyama/xxx.h"。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# googletest（vendored，源码在 third_parts/googletest）：
# 和 SDL / SDL_ttf 一样落进 third_parts，配置阶段不再联网。
# 之前用 FetchContent 直接从 GitHub 拉，网络一抖 CMake 就配置不下去。
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(third_parts/googletest EXCLUDE_FROM_ALL)
add_subdirectory(third_parts/SDL EXCLUDE_FROM_ALL)

# SDL3_ttf 3.2.2（vendored，源码在 third_parts/SDL_ttf）：
# 字体光栅化。freetype 2.13.3 也在 third_parts/SDL_ttf/external/freetype，
# harfbuzz / plutosvg 关掉 —— 图表标签不需要复杂 shaping 与彩色 SVG 字形，
# 关掉能省一个 C++ 依赖和一部分 wasm 体积。
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_WERROR OFF CACHE BOOL "" FORCE)
set(SDLTTF_STRICT OFF CACHE BOOL "" FORCE)
add_subdirectory(third_parts/SDL_ttf EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------- 字体清单
# 只有 KIRIYAMA_FONTS 点名的字体才会打进 wasm（默认 JetBrainsMono-Light）。
# 解析结果同时喂给 --preload-file 和 Fonts.generated.h，两端不会对不上。
include(cmake/Fonts.cmake)
kiriyama_resolve_fonts(KIRIYAMA_FONT_PATHS KIRIYAMA_FONT_NAMES)
kiriyama_generate_font_header("${KIRIYAMA_FONT_NAMES}")
kiriyama_font_bytes(KIRIYAMA_FONT_KB)
string(JOIN ", " KIRIYAMA_FONT_LIST ${KIRIYAMA_FONT_NAMES})
message(STATUS "kiriyama: 打包字体 [${KIRIYAMA_FONT_LIST}] 共 ${KIRIYAMA_FONT_KB} KB")

add_library(kiriyama_fonts INTERFACE)
target_include_directories(kiriyama_fonts INTERFACE "${CMAKE_BINARY_DIR}/generated")

add_subdirectory(math)
add_subdirectory(core)
# platform/ 是平台层：src/ 是跨平台组件与渲染层，web/ 是 web 平台的
# 统一 C API（c_api.cc）与宿主。以后 native/、android/ 等同级并列。
add_subdirectory(platform)

