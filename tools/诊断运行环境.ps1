# 启动失败现场收集：典型症状是
#   "This application failed to start because no Qt platform plugin could be initialized"
# 只读诊断，不改任何文件；第 4 段会启动一次程序并在 5 秒后关掉。
# 用法：powershell -ExecutionPolicy Bypass -File tools\诊断运行环境.ps1 [-Configuration Debug]
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Continue'
$diagRepo = Split-Path $PSScriptRoot -Parent
$diagConfig = $Configuration.ToLowerInvariant()
$diagDir = Join-Path $diagRepo ('build\' + $diagConfig)
$diagExe = Join-Path $diagDir 'lingya.exe'

"===== 灵芽思维启动诊断 $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ====="
"仓库：$diagRepo"
"配置：$Configuration"

"--- 1. 程序与运行库 ---"
if (-not (Test-Path -LiteralPath $diagExe -PathType Leaf)) {
    "  [缺] $diagExe —— 尚未构建，先跑 cmake --build --preset $diagConfig"
} else {
    $diagItem = Get-Item -LiteralPath $diagExe
    "  exe：$($diagItem.FullName)"
    "       大小 $($diagItem.Length) 字节，修改于 $($diagItem.LastWriteTime)"
    foreach ($diagName in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms\qwindows.dll')) {
        $diagPath = Join-Path $diagDir $diagName
        if (Test-Path -LiteralPath $diagPath -PathType Leaf) {
            $diagFile = Get-Item -LiteralPath $diagPath
            "  [有] $diagName  $($diagFile.Length) 字节  $($diagFile.LastWriteTime)"
        } else {
            "  [缺] $diagName"
        }
    }
}

"--- 2. 当前进程里的相关变量（干扰项） ---"
foreach ($diagVar in @('QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_FONTDIR', 'QT_DEBUG_PLUGINS', 'LINGYA_TOOLS_ROOT', 'QT_KIT_DIR')) {
    $diagValue = [Environment]::GetEnvironmentVariable($diagVar)
    if ([string]::IsNullOrEmpty($diagValue)) {
        "  $diagVar = <未设置>"
    } else {
        "  $diagVar = $diagValue"
    }
}

"--- 3. 持久化变量（用户 / 系统） ---"
foreach ($diagScope in @('User', 'Machine')) {
    $diagAll = [Environment]::GetEnvironmentVariables($diagScope)
    $diagHits = @()
    foreach ($diagKey in $diagAll.Keys) {
        if ($diagKey -match '^QT_') { $diagHits += "$diagKey=$($diagAll[$diagKey])" }
    }
    if ($diagHits.Count -eq 0) {
        "  $diagScope：无 QT_* 变量"
    } else {
        "  $diagScope：" + ($diagHits -join '; ')
    }
}

"--- 4. 清除干扰变量后启动一次 ---"
foreach ($diagVar in @('QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH')) {
    Remove-Item ('Env:' + $diagVar) -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $diagExe -PathType Leaf)) {
    "  跳过：exe 不存在"
} else {
    $diagProc = Start-Process -FilePath $diagExe -WorkingDirectory $diagDir -PassThru
    Start-Sleep -Seconds 5
    if ($diagProc.HasExited) {
        "  结果：进程已退出，退出码 $($diagProc.ExitCode)"
    } else {
        if ([string]::IsNullOrWhiteSpace($diagProc.MainWindowTitle)) {
            "  结果：进程存活（PID $($diagProc.Id)）但没有主窗口 —— 很可能正弹着错误对话框"
        } else {
            "  结果：正常，窗口「$($diagProc.MainWindowTitle)」"
        }
        Stop-Process -Id $diagProc.Id -Force
        "  （已关闭测试进程）"
    }
}

"--- 5. 怎么读这份输出 ---"
"  1) 第 1 段出现 [缺]：重跑 tools\准备运行依赖.ps1 -Configuration $Configuration"
"  2) 第 2/3 段 QT_QPA_PLATFORM 有值（尤其 offscreen）：该终端已带测试平台，先清掉再启动；第 4 段已自动清"
"  3) 第 2/3 段 QT_PLUGIN_PATH 指向不存在的目录：改用 tools\打开灵芽思维.ps1 启动（会在启动前清掉）"
"  4) 文件齐全、第 4 段仍失败：把这整段输出发出来，再查是否混入了别的 Qt 版本的 qwindows.dll"
