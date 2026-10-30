# 在当前终端临时启用冻结工具链，不修改系统或用户的永久 PATH。
[CmdletBinding()]
param(
    [string]$ToolsRoot = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) '.tools')
)

$ErrorActionPreference = 'Stop'
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
$env:QT_PLUGIN_PATH = Join-Path $taskQtKit 'plugins'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $taskQtKit 'plugins\platforms'
$taskPrefix = @((Join-Path $taskQtKit 'bin'), $taskMingw, $taskCmake, $taskNinja, $taskFormat)
$taskOtherPaths = @($env:PATH -split ';' | Where-Object { $_ -and ($_ -notin $taskPrefix) })
$env:PATH = (@($taskPrefix) + @($taskOtherPaths)) -join ';'
Set-Location -LiteralPath $taskRepoRoot
Write-Host '已在当前终端启用灵芽思维冻结环境；关闭终端即恢复原来的环境。'
Write-Host 'Qt 6.8.3 / MinGW 13.1.0 / CMake 3.30.5 / Ninja 1.12.1'
