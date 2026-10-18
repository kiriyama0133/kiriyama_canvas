#!/usr/bin/env bash
# kiriyama_canvas 构建脚本（Emscripten / wasm）
#
# 参数就是要打包进 wasm 的字体，默认只带 JetBrainsMono-Light。
# 字体清单会同时喂给 --preload-file 和 Fonts.generated.h，
# 所以"构建带了什么"和"运行时能读到什么"永远一致。
#
#   ./scripts/build.sh                                  # 默认 JetBrainsMono-Light
#   ./scripts/build.sh JetBrainsMono-Bold               # 换一支
#   ./scripts/build.sh JetBrainsMono-Light fontawesome-webfont
#   ./scripts/build.sh --all                            # public/ 下全部
#   ./scripts/build.sh --debug --test                   # Debug + 单元测试
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Git Bash / MSYS 下 pwd 给的是 /e/xxx 这种 POSIX 路径，而 cmake 是原生程序，
# 传过去它会说"source directory does not exist"。转成 E:/xxx 混合式路径，
# bash 和 cmake 都能认。
if command -v cygpath >/dev/null 2>&1; then
    ROOT="$(cygpath -m "$ROOT")"
fi

BUILD_DIR="${ROOT}/build"
DEFAULT_FONT="JetBrainsMono-Light"

FONTS=()
CONFIG="Release"
WITH_TESTS=0
CLEAN=0
JOBS=""
ALL=0

usage() {
    cat <<'EOF'
用法: scripts/build.sh [字体...] [选项]

字体（可多个，可省略 .ttf，大小写不敏感；缺省为 JetBrainsMono-Light）:
  scripts/build.sh                                   只带 JetBrainsMono-Light
  scripts/build.sh JetBrainsMono-Bold                换一支
  scripts/build.sh JetBrainsMono-Light fontawesome-webfont
  scripts/build.sh --all                             public/ 下全部字体

选项:
  --release          Release 构建（默认；体积小、跑得快）
  --debug            Debug 构建（带 DWARF + 断言，wasm 会大 7 倍）
  --test             同时构建并运行单元测试
  --clean            先删掉 build/ 再配置（会重新编译全部依赖，较慢）
  -j N, --jobs N     并行数
  -h, --help         显示本帮助
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)        usage; exit 0 ;;
        --all)            ALL=1 ;;
        --release)        CONFIG="Release" ;;
        --debug)          CONFIG="Debug" ;;
        --test|-t)        WITH_TESTS=1 ;;
        --clean)          CLEAN=1 ;;
        -j|--jobs)        JOBS="${2:-}"; shift ;;
        --jobs=*)         JOBS="${1#*=}" ;;
        -*)               echo "未知选项: $1" >&2; usage; exit 2 ;;
        *)
            # 允许一个参数里用逗号分隔多支字体
            IFS=',' read -r -a parts <<< "$1"
            for part in "${parts[@]}"; do
                [ -n "$part" ] && FONTS+=("$part")
            done
            ;;
    esac
    shift
done

if [ "$ALL" -eq 1 ]; then
    FONTS=("all")
fi
if [ "${#FONTS[@]}" -eq 0 ]; then
    FONTS=("$DEFAULT_FONT")
fi

SELECTION="$(IFS=,; echo "${FONTS[*]}")"

# ---------------------------------------------------------------- emsdk
find_emsdk() {
    local candidates=()
    [ -n "${EMSDK:-}" ] && candidates+=("$EMSDK")
    candidates+=(
        "$ROOT/../emsdk"
        "/e/emsdk" "/c/emsdk" "/d/emsdk"
        "$HOME/emsdk"
        "/opt/emsdk" "/usr/local/emsdk"
    )
    local candidate
    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" ]; then
            printf '%s' "$candidate"
            return
        fi
    done
    printf ''
}

EMSDK_ROOT="$(find_emsdk)"
TOOLCHAIN=""
if [ -n "$EMSDK_ROOT" ]; then
    TOOLCHAIN="$EMSDK_ROOT/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
    export EMSDK="$EMSDK_ROOT"
    case ":$PATH:" in
        *":$EMSDK_ROOT/upstream/emscripten:"*) ;;
        *) export PATH="$EMSDK_ROOT/upstream/emscripten:$PATH" ;;
    esac
fi

if [ "$CLEAN" -eq 1 ]; then
    if [ "$(basename "$BUILD_DIR")" != "build" ]; then
        echo "拒绝清理: $BUILD_DIR 不像构建目录" >&2
        exit 1
    fi
    echo "==> 清理 $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# 全新配置又找不到 emsdk 时，只能靠 build/ 里已有的缓存。
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && [ -z "$TOOLCHAIN" ]; then
    echo "找不到 emsdk（没有 -DCMAKE_TOOLCHAIN_FILE 可用）。" >&2
    echo "请设置 EMSDK 环境变量，或把 emsdk 放在 $ROOT/../emsdk。" >&2
    exit 1
fi

# ---------------------------------------------------------------- 配置
CMAKE_ARGS=(
    -S "$ROOT"
    -B "$BUILD_DIR"
    -G Ninja
    "-DCMAKE_BUILD_TYPE=$CONFIG"
    "-DKIRIYAMA_FONTS=$SELECTION"
)

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    [ -n "$TOOLCHAIN" ] && CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN")
    if ! command -v ninja >/dev/null 2>&1; then
        # 没装 ninja 就借用 VS 自带的那份
        for candidate in "/c/Program Files (x86)/Microsoft Visual Studio/2022"/*/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe \
                         "/c/Program Files/Microsoft Visual Studio/2022"/*/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe; do
            if [ -x "$candidate" ]; then
                CMAKE_ARGS+=("-DCMAKE_MAKE_PROGRAM=$candidate")
                break
            fi
        done
    fi
fi

echo "==> 字体: $SELECTION  ($CONFIG)"
cmake "${CMAKE_ARGS[@]}"

# ---------------------------------------------------------------- 构建
TARGETS=(kiriyama_text_demo kiriyama_table_demo)
[ "$WITH_TESTS" -eq 1 ] && TARGETS+=(kiriyama_text_tests)

BUILD_ARGS=(--build "$BUILD_DIR" --target "${TARGETS[@]}")
[ -n "$JOBS" ] && BUILD_ARGS+=(--parallel "$JOBS")

echo "==> 构建: ${TARGETS[*]}"
cmake "${BUILD_ARGS[@]}"

# ---------------------------------------------------------------- 测试
if [ "$WITH_TESTS" -eq 1 ]; then
    find_node() {
        if command -v node >/dev/null 2>&1; then command -v node; return; fi
        if [ -n "$EMSDK_ROOT" ]; then
            local candidate
            for candidate in "$EMSDK_ROOT"/node/*/bin/node "$EMSDK_ROOT"/node/*/bin/node.exe; do
                [ -x "$candidate" ] && { printf '%s' "$candidate"; return; }
            done
        fi
        printf ''
    }

    NODE="$(find_node)"
    if [ -z "$NODE" ]; then
        echo "找不到 node，跳过测试。产物在 $BUILD_DIR/src/tests/" >&2
    else
        echo "==> 测试"
        # .data 是按当前工作目录找的，必须在产物目录里跑
        ( cd "$BUILD_DIR/src/tests" && "$NODE" kiriyama_text_tests.js )
    fi
fi

# ---------------------------------------------------------------- 汇报
size_of() {
    if [ -f "$1" ]; then
        printf '%8s  %s\n' "$(du -h "$1" | cut -f1)" "${1#"$ROOT"/}"
    fi
}

echo
echo "==> 产物"
size_of "$ROOT/web/index.html"
size_of "$ROOT/web/index.js"
size_of "$ROOT/web/index.wasm"
size_of "$ROOT/web/index.data"
echo
echo "本地预览: python -m http.server 8765 --bind 127.0.0.1 --directory web"
