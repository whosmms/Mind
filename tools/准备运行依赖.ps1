# 将当前Kit的运行库放到构建目录，支持直接启动exe；不修改永久PATH。
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    # 留空则由 进入开发环境.ps1 自动探测工具链根目录。
    [string]$ToolsRoot = ''
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
    # WinPS 5.1 在 $ErrorActionPreference='Stop' 下会把 windeployqt 写到 stderr 的普通警告
    # （如 "Translations will not be available..."）当成异常抛出，脚本在部署中途中断，
    # 于是构建目录留下半套依赖，启动时就报 no Qt platform plugin could be initialized。
    # 这里临时放宽错误动作，改为按退出码判定，失败时把真实输出带进异常。
    $deployPrevErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $deployOutput = (& $deployTool ('--' + $deployQtFlavor) --compiler-runtime --no-translations $deployExe 2>&1) -join "`n"
    $deployCode = $LASTEXITCODE
    $ErrorActionPreference = $deployPrevErrorAction
    if ($deployCode -ne 0) {
        throw "准备${deployConfig}运行依赖失败（windeployqt 退出码 $deployCode）：`n$deployOutput"
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
    # windeployqt只部署与当前会话匹配的那个平台插件，构建目录里通常只有qwindows。
    # 自动测试用QT_QPA_PLATFORM=offscreen；插件不在程序目录、PATH又没Qt时，Qt找不到平台插件会
    # 弹模态框并挂住进程（命令行跑测试也一样，屏幕上会凭空多出一个报错窗口）。
    # 因此额外补齐offscreen与minimal，让构建目录自洽。
    $deployExtraPlatforms = Join-Path (Join-Path $env:QT_KIT_DIR 'plugins') 'platforms'
    foreach ($deployPlatform in @('qoffscreen', 'qminimal')) {
        $deployPlatformSource = Join-Path $deployExtraPlatforms ($deployPlatform + '.dll')
        if (-not (Test-Path -LiteralPath $deployPlatformSource -PathType Leaf)) {
            Write-Host "提示：Qt Kit 未提供 ${deployPlatform}.dll，跳过（${deployConfig}）。"
            continue
        }
        $deployPlatformTarget = Join-Path (Join-Path $deployDirectory 'platforms') ($deployPlatform + $deploySuffix + '.dll')
        if (-not (Test-Path -LiteralPath $deployPlatformTarget) -or
            (Get-FileHash -LiteralPath $deployPlatformSource).Hash -ne (Get-FileHash -LiteralPath $deployPlatformTarget).Hash) {
            Copy-Item -LiteralPath $deployPlatformSource -Destination $deployPlatformTarget -Force
        }
    }
    # 测试 exe 在 build\<cfg>\tests\，而 Qt 只找「可执行文件同级 platforms」，不会回落到
    # build\<cfg>\platforms。直接跑 tests\tst_*.exe 时找不到插件就弹模态框挂住（屏幕上多一个
    # 「no Qt platform plugin」窗口，命令行还看不到输出）。这里把 platforms 同步一份到 tests\。
    $deployTestsDir = Join-Path $deployDirectory 'tests'
    if (Test-Path -LiteralPath $deployTestsDir -PathType Container) {
        $deployTestsPlatforms = Join-Path $deployTestsDir 'platforms'
        if (-not (Test-Path -LiteralPath $deployTestsPlatforms -PathType Container)) {
            New-Item -ItemType Directory -Path $deployTestsPlatforms -Force | Out-Null
        }
        foreach ($deployPlugin in (Get-ChildItem -LiteralPath (Join-Path $deployDirectory 'platforms') -Filter '*.dll' -File)) {
            $deployPluginTarget = Join-Path $deployTestsPlatforms $deployPlugin.Name
            if (-not (Test-Path -LiteralPath $deployPluginTarget -PathType Leaf) -or
                (Get-FileHash -LiteralPath $deployPlugin.FullName).Hash -ne (Get-FileHash -LiteralPath $deployPluginTarget).Hash) {
                Copy-Item -LiteralPath $deployPlugin.FullName -Destination $deployPluginTarget -Force
            }
        }
    }
    foreach ($deployRequired in @("Qt6Core${deploySuffix}.dll", "Qt6Gui${deploySuffix}.dll", "Qt6Widgets${deploySuffix}.dll", "platforms\qwindows${deploySuffix}.dll")) {
        if (-not (Test-Path -LiteralPath (Join-Path $deployDirectory $deployRequired) -PathType Leaf)) {
            throw "部署后仍缺少必要文件：$deployRequired"
        }    }
    Write-Host "${deployConfig}运行依赖已准备（Qt ${deployQtFlavor}），可直接打开${deployExe}。请保留整个构建目录中的DLL与插件。"
}
