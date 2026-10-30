# 在当前终端临时启用冻结工具链，不修改系统或用户的永久 PATH。
[CmdletBinding()]
param(
    # 留空则自动探测：本项目 .tools → D:\ → C:\Qt → C:\ → PATH 中的 qmake 反推。
    [string]$ToolsRoot = ''
)

$ErrorActionPreference = 'Stop'

# 冻结工具链以 <root>\Qt\6.8.3\mingw_64\bin\qmake.exe 为唯一判据。
function Test-LingyaToolsRoot {
    param([string]$Root)
    if ([string]::IsNullOrWhiteSpace($Root)) { return $false }
    $probe = Join-Path $Root 'Qt\6.8.3\mingw_64\bin\qmake.exe'
    return (Test-Path -LiteralPath $probe -PathType Leaf)
}

if ([string]::IsNullOrWhiteSpace($ToolsRoot)) {
    $taskCandidates = New-Object System.Collections.Generic.List[string]
    if ($env:LINGYA_TOOLS_ROOT) { $taskCandidates.Add($env:LINGYA_TOOLS_ROOT) }
    $taskCandidates.Add((Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) '.tools'))
    $taskCandidates.Add('D:\')
    $taskCandidates.Add('C:\Qt')
    $taskCandidates.Add('C:\')
    $taskQmakeOnPath = Get-Command 'qmake.exe' -ErrorAction SilentlyContinue
    if ($taskQmakeOnPath) {
        # <root>\Qt\6.8.3\mingw_64\bin\qmake.exe 逐级上推五层得到 <root>
        $taskPathRoot = Split-Path (Split-Path (Split-Path (Split-Path (Split-Path $taskQmakeOnPath.Source -Parent) -Parent) -Parent) -Parent) -Parent
        if ($taskPathRoot) { $taskCandidates.Add($taskPathRoot) }
    }
    foreach ($taskCandidate in $taskCandidates) {
        if (Test-LingyaToolsRoot $taskCandidate) { $ToolsRoot = $taskCandidate; break }
    }
    if ([string]::IsNullOrWhiteSpace($ToolsRoot)) {
        $taskTried = (@($taskCandidates) | Where-Object { $_ } | Select-Object -Unique) -join '、'
        throw "未找到 Qt 6.8.3 冻结工具链（按 <root>\Qt\6.8.3\mingw_64\bin\qmake.exe 判定）。已尝试：$taskTried。请用 -ToolsRoot 指定安装根目录，例如 -ToolsRoot D:\"
    }
    Write-Host "已自动定位工具链根目录：$ToolsRoot"
}

$taskToolsRoot = [IO.Path]::GetFullPath($ToolsRoot)
$taskRepoRoot = Split-Path $PSScriptRoot -Parent
$taskQtKit = Join-Path $taskToolsRoot 'Qt\6.8.3\mingw_64'
$taskMingw = Join-Path $taskToolsRoot 'Qt\Tools\mingw1310_64\bin'
$taskCmake = Join-Path $taskToolsRoot 'Qt\Tools\CMake_64\bin'
$taskNinja = Join-Path $taskToolsRoot 'Qt\Tools\Ninja'
$taskFormat = Join-Path $taskToolsRoot 'formatter\clang_format\data\bin'

$taskRequired = @(
    (Join-Path $taskQtKit 'bin\qmake.exe'),
    (Join-Path $taskMingw 'g++.exe'),
    (Join-Path $taskCmake 'cmake.exe'),
    (Join-Path $taskNinja 'ninja.exe'),
    (Join-Path $taskFormat 'clang-format.exe')
)
foreach ($taskRequiredFile in $taskRequired) {
    if (-not (Test-Path -LiteralPath $taskRequiredFile -PathType Leaf)) {
        throw "缺少冻结工具：$taskRequiredFile。请先按 docs/environment.md 安装，或用 -ToolsRoot 指定安装根目录。"
    }
}

$env:LINGYA_TOOLS_ROOT = $taskToolsRoot
$env:QT_KIT_DIR = $taskQtKit
# 这两个变量只服务"未部署依赖的开发终端"（如直接跑构建目录里的 exe、clang-format 等）。
# 它们会参与 Qt 的插件搜索，一旦指向缺失目录，GUI 就会报
# "no Qt platform plugin could be initialized"；因此 tools\打开灵芽思维.ps1 启动前会清掉它们，
# 让程序使用自己目录下 windeployqt 部署的 platforms\qwindows.dll。
$env:QT_PLUGIN_PATH = Join-Path $taskQtKit 'plugins'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $taskQtKit 'plugins\platforms'
$taskPrefix = @((Join-Path $taskQtKit 'bin'), $taskMingw, $taskCmake, $taskNinja, $taskFormat)
$taskOtherPaths = @($env:PATH -split ';' | Where-Object { $_ -and ($_ -notin $taskPrefix) })
$env:PATH = (@($taskPrefix) + @($taskOtherPaths)) -join ';'
Set-Location -LiteralPath $taskRepoRoot
Write-Host '已在当前终端启用灵芽思维冻结环境；关闭终端即恢复原来的环境。'
Write-Host 'Qt 6.8.3 / MinGW 13.1.0 / CMake 3.30.5 / Ninja 1.12.1'
