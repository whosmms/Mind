# 将当前Kit的运行库放到构建目录，支持直接启动exe；不修改永久PATH。
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    [string]$ToolsRoot = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) '.tools')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '进入开发环境.ps1') -ToolsRoot $ToolsRoot
$deployRepo = Split-Path $PSScriptRoot -Parent
$deployTool = Join-Path $env:QT_KIT_DIR 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw '未找到当前Qt Kit的windeployqt.exe，请核对qttools组件。'
}
$deployConfigs = if ($Configuration -eq 'Both') { @('debug', 'release') } else { @($Configuration.ToLowerInvariant()) }
foreach ($deployConfig in $deployConfigs) {
    $deployDirectory = Join-Path $deployRepo ('build\' + $deployConfig)
    $deployExe = Join-Path $deployDirectory 'lingya.exe'
    if (-not (Test-Path -LiteralPath $deployExe -PathType Leaf)) {
        throw "尚未构建${deployConfig}，请先执行对应CMake构建。"
    }
    # C++的Debug配置不等于链接了Debug Qt；当前MinGW Kit只提供无d后缀的Qt运行库。
    # 以PE实际导入为准，避免--debug误找不存在的qwindowsd.dll。
    $deployMingw = Join-Path $env:LINGYA_TOOLS_ROOT 'Qt\Tools\mingw1310_64\bin'
    $deployImports = (& (Join-Path $deployMingw 'objdump.exe') -p $deployExe) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw '读取程序依赖失败，未猜测Qt运行库类型。' }
    if ($deployImports -match 'DLL Name:\s+Qt6Cored\.dll') { $deployQtFlavor = 'debug' }
    elseif ($deployImports -match 'DLL Name:\s+Qt6Core\.dll') { $deployQtFlavor = 'release' }
    else { throw '未识别到Qt Core运行依赖，请核对本项目的动态链接Kit。' }
    & $deployTool ('--' + $deployQtFlavor) --compiler-runtime --no-translations $deployExe
    if ($LASTEXITCODE -ne 0) {
        throw "准备${deployConfig}运行依赖失败，请查看windeployqt实际输出。"
    }
    # 明确从配套MinGW取得运行库，避免某些Qt发行包仅搜索Qt/bin而漏拷贝。
    foreach ($deployRuntime in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
        $deploySource = Join-Path $deployMingw $deployRuntime
        if (-not (Test-Path -LiteralPath $deploySource -PathType Leaf)) {
            throw "配套MinGW缺少运行库：$deployRuntime"
        }
        $deployTarget = Join-Path $deployDirectory $deployRuntime
        if (-not (Test-Path -LiteralPath $deployTarget) -or
            (Get-FileHash -LiteralPath $deploySource).Hash -ne (Get-FileHash -LiteralPath $deployTarget).Hash) {
            Copy-Item -LiteralPath $deploySource -Destination $deployTarget -Force
        }
    }
    $deploySuffix = if ($deployQtFlavor -eq 'debug') { 'd' } else { '' }
    foreach ($deployRequired in @("Qt6Core${deploySuffix}.dll", "Qt6Gui${deploySuffix}.dll", "Qt6Widgets${deploySuffix}.dll", "platforms\qwindows${deploySuffix}.dll")) {
        if (-not (Test-Path -LiteralPath (Join-Path $deployDirectory $deployRequired) -PathType Leaf)) {
            throw "部署后仍缺少必要文件：$deployRequired"
        }
    }
    Write-Host "${deployConfig}运行依赖已准备（Qt ${deployQtFlavor}），可直接打开${deployExe}。请保留整个构建目录中的DLL与插件。"
}
