# 开发用入口：先构建、补齐当前Kit依赖，再启动现有工程。
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    # 留空则由 进入开发环境.ps1 自动探测工具链根目录。
    [string]$ToolsRoot = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '进入开发环境.ps1') -ToolsRoot $ToolsRoot
$launchRepo = Split-Path $PSScriptRoot -Parent
$launchConfig = $Configuration.ToLowerInvariant()
$launchExe = Join-Path $launchRepo ('build\' + $launchConfig + '\lingya.exe')
$launchExisting = @(Get-Process -Name lingya -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $launchExe })
if ($launchExisting.Count -gt 0) {
    Write-Host '此配置的灵芽思维已在运行，请切换到现有窗口；重新构建前先正常关闭它。'
    return
}
& cmake --preset $launchConfig
if ($LASTEXITCODE -ne 0) { throw '配置失败，未启动旧程序。' }
& cmake --build --preset $launchConfig --parallel 2
if ($LASTEXITCODE -ne 0) { throw '构建失败，未启动旧程序；如程序已打开，请先正常关闭窗口。' }
& (Join-Path $PSScriptRoot '准备运行依赖.ps1') -Configuration $Configuration -ToolsRoot $ToolsRoot
if (-not $?) { throw '运行依赖准备失败，未启动程序。' }
# 启动 GUI 前必须清掉三类变量，否则 Qt 会去别处找平台插件，报
# "This application failed to start because no Qt platform plugin could be initialized"：
#   QT_QPA_PLATFORM=offscreen —— 自动测试专用，构建目录只部署了 qwindows；
#   QT_PLUGIN_PATH / QT_QPA_PLATFORM_PLUGIN_PATH —— 由 进入开发环境.ps1 按 Kit 设置，
#   Kit 路径变化或被其它 Qt 覆盖时同样会把搜索带偏。程序目录自带完整插件，不需要它们。
foreach ($launchVar in @('QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH')) {
    Remove-Item ('Env:' + $launchVar) -ErrorAction SilentlyContinue
}

$launchDir = Split-Path $launchExe -Parent
$launchPlatform = Join-Path $launchDir 'platforms\qwindows.dll'
if (-not (Test-Path -LiteralPath $launchPlatform -PathType Leaf)) {
    throw "缺少平台插件：$launchPlatform。请重跑 tools\准备运行依赖.ps1 -Configuration $Configuration，或运行 tools\诊断运行环境.ps1 看完整现场。"
}

Start-Process -FilePath $launchExe -WorkingDirectory $launchDir -WindowStyle Normal

# 启动自检：3 秒内退出说明启动即失败（最常见仍是依赖/插件），直接指路，不让用户对着弹窗猜。
Start-Sleep -Seconds 3
$launchLive = @(Get-Process -Name lingya -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $launchExe })
if ($launchLive.Count -eq 0) {
    Write-Warning '程序启动后立即退出，通常是依赖或平台插件问题。请运行 tools\诊断运行环境.ps1，把输出发出来。'
    return
}
if ([string]::IsNullOrWhiteSpace($launchLive[0].MainWindowTitle)) {
    Write-Warning "进程在运行（PID $($launchLive[0].Id)）但还没有主窗口，可能弹出了错误对话框；同样请跑 tools\诊断运行环境.ps1。"
    return
}
Write-Host "灵芽思维已启动（PID $($launchLive[0].Id)，窗口「$($launchLive[0].MainWindowTitle)」）。此入口用于开发验证，不是最终安装包。"
