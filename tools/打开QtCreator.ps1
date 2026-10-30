# 使用本项目的独立Qt Creator设置，不修改其他Qt Creator安装或全局PATH。
[CmdletBinding()]
param(
    [string]$ToolsRoot = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) '.tools')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '进入开发环境.ps1') -ToolsRoot $ToolsRoot
$taskCreatorRoot = Join-Path $env:LINGYA_TOOLS_ROOT 'Qt\Tools\QtCreator'
$taskCreator = Join-Path $taskCreatorRoot 'bin\qtcreator.exe'
$taskProfile = Join-Path $env:LINGYA_TOOLS_ROOT 'qtcreator-profile'
$taskInstallSettings = Join-Path $taskCreatorRoot 'share\qtcreator'
$taskProject = Join-Path (Split-Path $PSScriptRoot -Parent) 'CMakeLists.txt'
if (-not (Test-Path -LiteralPath $taskCreator -PathType Leaf)) {
    throw '未找到项目专用Qt Creator，请先按环境记录安装。'
}
New-Item -ItemType Directory -Path $taskProfile -Force | Out-Null

# IDE有自己的Qt运行库，不能强制使用应用Qt6.8.3的插件目录。
Remove-Item Env:QT_PLUGIN_PATH -ErrorAction SilentlyContinue
Remove-Item Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
& $taskCreator -settingspath $taskProfile -installsettingspath $taskInstallSettings -language zh_CN $taskProject
