# platform：平台相关的代码容器。
#
#   src/   跨平台核心（math / core / scene / render）——组件与渲染层，
#          本身不认具体平台，也没有任何 demo（宿主归平台）。
#   web/   web 平台绑定：统一 C API（c_api.cc）+ 唯一宿主（CanvasHost.cc）。
#
# 以后加平台（native/、android/…）就在同级建目录，各自 add_subdirectory。
# 所有平台共用 platform/src 里的组件与渲染层。

# ---------------------------------------------------------------- 构建选项

# 是否构建 web 平台产物（wasm）。关掉只编组件与测试，适合纯 C++ 侧开发。
option(KIRIYAMA_BUILD_WEB "构建 web (wasm) 平台产物" ON)

# ---------------------------------------------------------------- wasm 公共标志
#
# 体积的大头是**构建类型**，不是代码量：Debug 会把 DWARF 调试信息和
# -sASSERTIONS 一起塞进产物，同一份代码 11 MB；Release 只有 1.6 MB。
# 所以两套标志分开，Debug 用来查错，Release 才是能发到网上的那一份。
# 放在 platform 作用域：src（组件与测试）和 web（产物）都能用。

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

add_subdirectory(src)
if(KIRIYAMA_BUILD_WEB)
    add_subdirectory(web)
endif()
