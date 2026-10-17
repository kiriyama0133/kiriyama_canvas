# 字体打包
#
# public/ 里可以放很多支字体，但一次构建通常只需要一两支。以前是
# `--preload-file=public@/public` 全量塞进 index.data，用户每加一支字体
# 产物就涨几百 KB。
#
# 现在改成白名单：只有 -DKIRIYAMA_FONTS 里点名的字体才会被 preload，
# 同时生成一份 Fonts.generated.h 告诉 C++ 代码"本次构建带了哪些字体"。
#
#   cmake -S . -B build -DKIRIYAMA_FONTS="JetBrainsMono-Light"
#   cmake -S . -B build -DKIRIYAMA_FONTS="JetBrainsMono-Light,fontawesome-webfont"
#   cmake -S . -B build -DKIRIYAMA_FONTS=all
#
# 名字带不带扩展名都行，大小写不敏感。

set(KIRIYAMA_FONT_DIR "${CMAKE_SOURCE_DIR}/public"
    CACHE PATH "字体目录（默认 public/）")

set(KIRIYAMA_FONTS "JetBrainsMono-Light"
    CACHE STRING "要打包进 wasm 的字体，逗号或分号分隔；all 表示全带")

# 解析 KIRIYAMA_FONTS，产出：
#   out_paths —— 字体文件的绝对路径（给 --preload-file 用）
#   out_names —— 文件名（如 JetBrainsMono-Light.ttf）
function(kiriyama_resolve_fonts out_paths out_names)
    file(GLOB font_files CONFIGURE_DEPENDS
        "${KIRIYAMA_FONT_DIR}/*.ttf"
        "${KIRIYAMA_FONT_DIR}/*.otf"
        "${KIRIYAMA_FONT_DIR}/*.ttc")
    list(SORT font_files)

    set(available "")
    foreach(path IN LISTS font_files)
        get_filename_component(name "${path}" NAME)
        list(APPEND available "${name}")
    endforeach()

    if(NOT available)
        message(FATAL_ERROR
            "KIRIYAMA_FONT_DIR (${KIRIYAMA_FONT_DIR}) 下没有 .ttf/.otf/.ttc 字体文件。")
    endif()

    # 逗号、分号、空格都当分隔符，脚本传什么都行。
    set(tokens "${KIRIYAMA_FONTS}")
    string(REPLACE "," ";" tokens "${tokens}")
    string(REPLACE " " ";" tokens "${tokens}")

    set(selected "")
    foreach(token IN LISTS tokens)
        string(STRIP "${token}" token)
        if(token STREQUAL "")
            continue()
        endif()

        string(TOLOWER "${token}" token_lower)
        if(token_lower STREQUAL "all")
            foreach(name IN LISTS available)
                list(APPEND selected "${name}")
            endforeach()
            continue()
        endif()

        # 允许省略扩展名：JetBrainsMono-Light -> JetBrainsMono-Light.ttf
        get_filename_component(token_ext "${token}" EXT)
        if(token_ext STREQUAL "")
            set(candidates "${token}.ttf" "${token}.otf" "${token}.ttc")
        else()
            set(candidates "${token}")
        endif()

        set(hit "")
        foreach(candidate IN LISTS candidates)
            string(TOLOWER "${candidate}" candidate_lower)
            foreach(name IN LISTS available)
                string(TOLOWER "${name}" name_lower)
                if(name_lower STREQUAL candidate_lower)
                    set(hit "${name}")
                    break()
                endif()
            endforeach()
            if(hit)
                break()
            endif()
        endforeach()

        if(NOT hit)
            string(JOIN "\n    " listing ${available})
            message(FATAL_ERROR
                "KIRIYAMA_FONTS 里的 \"${token}\" 在 ${KIRIYAMA_FONT_DIR} 下找不到。\n"
                "可用的字体：\n    ${listing}")
        endif()

        list(APPEND selected "${hit}")
    endforeach()

    if(NOT selected)
        message(FATAL_ERROR
            "KIRIYAMA_FONTS 为空。至少选一支字体，或传 all 表示全部。")
    endif()

    list(REMOVE_DUPLICATES selected)  # 保留首次出现的顺序

    set(paths "")
    foreach(name IN LISTS selected)
        list(APPEND paths "${KIRIYAMA_FONT_DIR}/${name}")
    endforeach()

    set(${out_paths} "${paths}" PARENT_SCOPE)
    set(${out_names} "${selected}" PARENT_SCOPE)
endfunction()

# 生成 Fonts.generated.h。内容没变就不写，避免每次 configure 都触发重编译。
function(kiriyama_generate_font_header names)
    # 正文字体：列表里第一支非图标字体。图标字体：名字带 awesome/icon/glyph 的那支。
    set(body "")
    set(icon "")
    foreach(name IN LISTS names)
        string(TOLOWER "${name}" lower)
        if(lower MATCHES "awesome|icon|glyph")
            if(NOT icon)
                set(icon "${name}")
            endif()
        elseif(NOT body)
            set(body "${name}")
        endif()
    endforeach()
    if(NOT body)
        set(body "${icon}")  # 只选了图标字体，那就用它
    endif()

    set(content
"// 由 cmake/Fonts.cmake 依据 -DKIRIYAMA_FONTS 生成，勿手改。\n\
// 换字体：cmake -S . -B build -DKIRIYAMA_FONTS=\"A;B\"  或  scripts/build.sh A B\n\
#pragma once\n\
\n\
#include <cstddef>\n\
#include <string_view>\n\
\n\
namespace kiriyama::render::embedded {\n\
\n\
/// 一支参与本次构建的字体（已 preload 进 wasm 虚拟文件系统）。\n\
struct Entry {\n\
    std::string_view file{};  // public/ 下的文件名，如 \"Roboto-Regular.ttf\"\n\
    std::string_view stem{};  // 去掉扩展名，UI 上显示用\n\
};\n\
\n\
inline constexpr Entry kFonts[] = {\n")

    foreach(name IN LISTS names)
        get_filename_component(stem "${name}" NAME_WE)
        string(APPEND content "    {\"${name}\", \"${stem}\"},\n")
    endforeach()

    string(APPEND content
"};\n\
\n\
inline constexpr std::size_t kCount = sizeof(kFonts) / sizeof(kFonts[0]);\n\
\n\
/// 默认正文字体：列表里第一支非图标字体。\n\
inline constexpr std::string_view kBodyFont{\"${body}\"};\n\
\n\
/// 图标字体：文件名含 awesome / icon / glyph 的那支，没有就是空串。\n\
inline constexpr std::string_view kIconFont{\"${icon}\"};\n\
\n\
}  // namespace kiriyama::render::embedded\n")

    set(path "${CMAKE_BINARY_DIR}/generated/kiriyama/render/Fonts.generated.h")
    set(existing "")
    if(EXISTS "${path}")
        file(READ "${path}" existing)
    endif()

    if(NOT existing STREQUAL content)
        file(WRITE "${path}" "${content}")
        message(STATUS "kiriyama: 字体清单已更新 -> ${path}")
    endif()
endfunction()

# 把选中的字体逐个 preload 到虚拟路径 /public/<文件名>。
# 逐个 preload 而不是整目录，是为了让"构建带了什么"和"运行时能读到什么"一致。
function(kiriyama_preload_fonts target)
    if(NOT EMSCRIPTEN)
        return()
    endif()

    foreach(path IN LISTS KIRIYAMA_FONT_PATHS)
        get_filename_component(name "${path}" NAME)
        target_link_options(${target} PRIVATE
            "--preload-file=${path}@/public/${name}")
    endforeach()
endfunction()

# 字体大小的可读字符串，构建结束时报一下。
function(kiriyama_font_bytes out)
    set(total 0)
    foreach(path IN LISTS KIRIYAMA_FONT_PATHS)
        file(SIZE "${path}" size)
        math(EXPR total "${total} + ${size}")
    endforeach()
    math(EXPR kb "${total} / 1024")
    set(${out} "${kb}" PARENT_SCOPE)
endfunction()
