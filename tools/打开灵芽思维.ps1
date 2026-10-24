# 开发用入口：先构建、补齐当前Kit依赖，再启动现有工程。
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$ToolsRoot = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) '.tools')
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
# 使用真实Windows平台；offscreen仅用于自动测试，不应让用户打开后看不到窗口。
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
Start-Process -FilePath $launchExe -WorkingDirectory (Split-Path $launchExe -Parent) -WindowStyle Normal
Write-Host '灵芽思维已启动。此入口用于开发验证，不是最终安装包。'
