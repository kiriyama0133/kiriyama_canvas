# web 平台：统一 C API + 唯一宿主。
#
#   include/kiriyama/web/c_api.h   所有组件的 C 接口声明（line/bar/table 都加这里）
#   c_api.cc                       对应实现（一个文件，别拆）
#   CanvasHost.cc                  唯一宿主：一个 wasm 里装 pie / sparkbar / table
#
# 产物落在仓库根的 web/：kiriyama_canvas.js / .wasm / .data，外加一份 LICENSE。
# 页面是 web/index.html——源页即交付页，自己 <script src="kiriyama_canvas.js">。
#
# 为什么只留一个目标：每个组件单开产物会把同一份字体和渲染层下载四五遍
# （每份 1.6MB）。现在一个产物装全部组件，页面用 kiriyama_show_component()
# 决定挂哪些、各占哪块矩形，wasm 只加载一次。

add_library(kiriyama_web_api STATIC c_api.cc)
target_include_directories(kiriyama_web_api PUBLIC include)
target_link_libraries(kiriyama_web_api PUBLIC kiriyama_render)

add_executable(kiriyama_canvas CanvasHost.cc)
target_link_libraries(kiriyama_canvas PRIVATE kiriyama_web_api kiriyama_fonts SDL3::SDL3)

if(EMSCRIPTEN)
    set_target_properties(kiriyama_canvas PROPERTIES
        OUTPUT_NAME "kiriyama_canvas"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web"
    )
    kiriyama_preload_fonts(kiriyama_canvas)

    # --post-js / --preload-file 的文件不在编译依赖图里：ninja 只认 .cc，
    # 改了 entry.js 会 "no work to do"，页面拿到的还是旧胶水。显式登记进去。
    set_property(TARGET kiriyama_canvas APPEND PROPERTY
        LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/entry.js")

    target_link_options(kiriyama_canvas PRIVATE
        "--post-js=${CMAKE_SOURCE_DIR}/web/entry.js"
        ${KIRIYAMA_WASM_COMMON}
        "-sEXPORTED_RUNTIME_METHODS=['ccall']"
    )
else()
    set_target_properties(kiriyama_canvas PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# LICENSE 跟着产物走：交付目录里 js/wasm/data 与许可证放在一起，
# 省得发布时单独找一份。copy_if_different：没变化就不动文件时间戳。
add_custom_command(TARGET kiriyama_canvas POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "$<TARGET_FILE_DIR:kiriyama_canvas>/LICENSE"
    COMMENT "复制 LICENSE 到产物目录"
)
