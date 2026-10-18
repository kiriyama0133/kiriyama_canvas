#Requires -Version 5.1
# 注意：本文件保存为 UTF-8 with BOM。Windows PowerShell 5.1 会把无 BOM 的 UTF-8
# 当 ANSI(GBK) 读，中文注释会被吃掉换行，整份脚本解析失败。
<#
.SYNOPSIS
    kiriyama_canvas 构建脚本（Emscripten / wasm）。

.DESCRIPTION
    参数就是要打包进 wasm 的字体，默认只带 JetBrainsMono-Light。
    字体清单会同时喂给 --preload-file 和 Fonts.generated.h，
    所以"构建带了什么"和"运行时能读到什么"永远一致。

.EXAMPLE
    .\scripts\build.ps1
    默认只打包 JetBrainsMono-Light。

.EXAMPLE
    .\scripts\build.ps1 JetBrainsMono-Bold
    换一支字体。

.EXAMPLE
    .\scripts\build.ps1 JetBrainsMono-Light fontawesome-webfont
    正文 + 图标字体。

.EXAMPLE
    .\scripts\build.ps1 --all --release --test
    public/ 下全部字体，Release，构建完顺手跑测试。
#>
[CmdletBinding()]
param(
    # 全部参数按顺序收下，自己解析：既支持 --release / --test 这种长选项，
    # 也支持直接写字体名。PowerShell 自带的参数绑定配 ValueFromRemainingArguments
    # 会把 "--test" 当位置参数吞进字体列表，所以这里不用它。
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]] $ArgList
)

# 这里必须是 Continue 而不是 Stop：PS 5.1 下原生命令往 stderr 写一行（比如
# gtest 用例里的 SDL_Log）在 Stop 模式下会变成 NativeCommandError 直接打断脚本。
# 所有失败一律靠 $LASTEXITCODE 判断。
$ErrorActionPreference = 'Continue'

# cmake / ninja 的输出是 UTF-8，控制台默认按 ANSI(GBK) 解，中文会变乱码。
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

function Show-Usage {
    Write-Host @'
用法: .\scripts\build.ps1 [字体...] [选项]

字体（可多个，可省略 .ttf，大小写不敏感；缺省为 JetBrainsMono-Light）:
  .\scripts\build.ps1                                   只带 JetBrainsMono-Light
  .\scripts\build.ps1 JetBrainsMono-Bold                换一支
  .\scripts\build.ps1 JetBrainsMono-Light fontawesome-webfont
  .\scripts\build.ps1 --all                             public/ 下全部字体

选项:
  --release          Release 构建（默认；体积小、跑得快）
  --debug            Debug 构建（带 DWARF + 断言，wasm 会大 7 倍）
  --test             同时构建并运行单元测试
  --clean            先删掉 build/ 再配置（会重新编译全部依赖，较慢）
  -j N, --jobs N     并行数
  -h, --help         显示本帮助
'@
}

# ---------------------------------------------------------------- 参数解析
$fonts = @()
$All = $false
# 默认 Release：Debug 会带 DWARF 和断言，wasm 体积差 7 倍。
$Config = 'Release'
$DoTest = $false
$Clean = $false
$Jobs = 0

for ($i = 0; $i -lt $ArgList.Count; ++$i) {
    $arg = $ArgList[$i]
    if ([string]::IsNullOrWhiteSpace($arg)) { continue }

    # 同时接受 -x 和 --x
    $flag = $arg -replace '^--?', ''

    if ($flag -match '^(all)$') {
        $All = $true
    } elseif ($flag -match '^(release)$') {
        $Config = 'Release'
    } elseif ($flag -match '^(debug)$') {
        $Config = 'Debug'
    } elseif ($flag -match '^(test|t)$') {
        $DoTest = $true
    } elseif ($flag -match '^(clean)$') {
        $Clean = $true
    } elseif ($flag -match '^(help|h|\?)$') {
        Show-Usage
        exit 0
    } elseif ($flag -match '^(jobs|j)=(.+)$') {
        $Jobs = [int]$Matches[2]
    } elseif ($flag -match '^(jobs|j)$') {
        if ($i + 1 -lt $ArgList.Count) { $Jobs = [int]$ArgList[++$i] }
    } elseif ($arg.StartsWith('-')) {
        throw "未知选项: $arg（用 --help 看用法）"
    } else {
        foreach ($part in ($arg -split ',')) {
            $trimmed = $part.Trim()
            if ($trimmed) { $fonts += $trimmed }
        }
    }
}

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root 'build'
$DefaultFont = 'JetBrainsMono-Light'

if ($All) { $fonts = @('all') }
if ($fonts.Count -eq 0) { $fonts = @($DefaultFont) }
$selection = ($fonts -join ',')

# ---------------------------------------------------------------- emsdk
function Find-Emsdk {
    $candidates = @()
    if ($env:EMSDK) { $candidates += $env:EMSDK }
    $candidates += @(
        (Join-Path $Root '..\emsdk'),
        'E:\emsdk', 'C:\emsdk', 'D:\emsdk',
        (Join-Path $HOME 'emsdk'),
        '/opt/emsdk', '/usr/local/emsdk'
    )
    foreach ($candidate in $candidates) {
        if (-not $candidate) { continue }
        $probe = Join-Path $candidate 'upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake'
        if (Test-Path $probe) { return (Resolve-Path $candidate).Path }
    }
    return $null
}

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { throw '找不到 cmake，请先安装并加入 PATH。' }

$EmsdkRoot = Find-Emsdk
$Toolchain = $null
if ($EmsdkRoot) {
    $Toolchain = Join-Path $EmsdkRoot 'upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake'
    $env:EMSDK = $EmsdkRoot
    $emscriptenBin = Join-Path $EmsdkRoot 'upstream\emscripten'
    if (($env:PATH -split ';') -notcontains $emscriptenBin) {
        $env:PATH = "$emscriptenBin;$env:PATH"
    }
}

if ($Clean) {
    if ((Split-Path $BuildDir -Leaf) -ne 'build') {
        throw "拒绝清理：$BuildDir 不像构建目录。"
    }
    Write-Host "==> 清理 $BuildDir"
    Remove-Item $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
}

$hasCache = Test-Path (Join-Path $BuildDir 'CMakeCache.txt')
if (-not $hasCache -and -not $Toolchain) {
    throw "找不到 emsdk。请设置 EMSDK 环境变量，或把 emsdk 放在 $Root\..\emsdk。"
}

# ---------------------------------------------------------------- 配置
$cmakeArgs = @(
    '-S', $Root,
    '-B', $BuildDir,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DKIRIYAMA_FONTS=$selection"
)
if (-not $hasCache) {
    if ($Toolchain) { $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" }

    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
        # 没装 ninja 就借用 VS 自带的那份
        $vsRoots = @(
            'C:\Program Files (x86)\Microsoft Visual Studio\2022',
            'C:\Program Files\Microsoft Visual Studio\2022'
        )
        foreach ($vsRoot in $vsRoots) {
            if (-not (Test-Path $vsRoot)) { continue }
            $found = Get-ChildItem -Path $vsRoot -Recurse -Filter 'ninja.exe' -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -like '*CommonExtensions*' } |
                Select-Object -First 1
            if ($found) {
                $cmakeArgs += "-DCMAKE_MAKE_PROGRAM=$($found.FullName)"
                break
            }
        }
    }
}

Write-Host "==> 字体: $selection  ($Config)"
& $cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake 配置失败（退出码 $LASTEXITCODE）。" }

# ---------------------------------------------------------------- 构建
$targets = @('kiriyama_canvas')
if ($DoTest) { $targets += 'kiriyama_text_tests' }

$buildArgs = @('--build', $BuildDir, '--target') + $targets
if ($Jobs -gt 0) { $buildArgs += @('--parallel', "$Jobs") }

Write-Host "==> 构建: $($targets -join ' ')"
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "构建失败（退出码 $LASTEXITCODE）。" }

# ---------------------------------------------------------------- 测试
if ($DoTest) {
    function Find-Node {
        $cmd = Get-Command node -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
        if ($EmsdkRoot) {
            $hit = Get-ChildItem -Path (Join-Path $EmsdkRoot 'node') -Recurse -Filter 'node.exe' -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($hit) { return $hit.FullName }
        }
        return $null
    }

    $node = Find-Node
    if (-not $node) {
        Write-Warning "找不到 node，跳过测试。产物在 $BuildDir\src\tests\"
    } else {
        Write-Host '==> 测试'
        # .data 是按当前工作目录找的，必须在产物目录里跑
        Push-Location (Join-Path $BuildDir 'platform\src\tests')
        try {
            & $node 'kiriyama_text_tests.js'
            if ($LASTEXITCODE -ne 0) { throw "测试失败（退出码 $LASTEXITCODE）。" }
        } finally {
            Pop-Location
        }
    }
}

# ---------------------------------------------------------------- 汇报
Write-Host ''
Write-Host '==> 产物'
foreach ($name in @('index.html', 'kiriyama_canvas.js', 'kiriyama_canvas.wasm',
                    'kiriyama_canvas.data', 'LICENSE')) {
    $path = Join-Path (Join-Path $Root 'web') $name
    if (Test-Path $path) {
        $kb = [math]::Round((Get-Item $path).Length / 1KB, 1)
        Write-Host ("  {0,9:N1} KB  web/{1}" -f $kb, $name)
    }
}
Write-Host ''
Write-Host '本地预览: python -m http.server 8765 --bind 127.0.0.1 --directory web'
