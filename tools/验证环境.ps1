# 只验证当前电脑，不代替另外两名成员的复现或真实界面验收。
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Both',
    [string]$ToolsRoot = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) '.tools')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '进入开发环境.ps1') -ToolsRoot $ToolsRoot
$taskValidationRepo = Split-Path $PSScriptRoot -Parent
$taskEvidenceDir = Join-Path $taskValidationRepo 'build\environment'
New-Item -ItemType Directory -Path $taskEvidenceDir -Force | Out-Null
$taskLogFile = Join-Path $taskEvidenceDir 'validation.log'
$taskReportFile = Join-Path $taskEvidenceDir 'validation.json'
$taskReport = [ordered]@{
    dateUtc = [DateTime]::UtcNow.ToString('o')
    computer = $env:COMPUTERNAME
    sourceCommit = ((git rev-parse HEAD) -join '').Trim()
    toolsRoot = $env:LINGYA_TOOLS_ROOT
    versions = [ordered]@{}
    configurations = @()
    success = $false
}
Start-Transcript -Path $taskLogFile -Force | Out-Null

function Invoke-CheckedTool {
    param([string]$Executable, [string[]]$ToolArguments)
    Write-Host ('执行：' + $Executable + ' ' + ($ToolArguments -join ' '))
    & $Executable @ToolArguments
    if ($LASTEXITCODE -ne 0) {
        throw "命令执行失败，退出码：$LASTEXITCODE。实际输出见 build/environment/validation.log。"
    }
}

try {
    $taskQtVersion = ((& qmake -query QT_VERSION) -join '').Trim()
    $taskGccVersion = ((& g++ -dumpfullversion) -join '').Trim()
    $taskCmakeVersion = ((& cmake --version | Select-Object -First 1) -join '').Trim()
    $taskNinjaVersion = ((& ninja --version) -join '').Trim()
    $taskFormatVersion = ((& clang-format --version) -join '').Trim()
    if ($taskQtVersion -ne '6.8.3' -or $taskGccVersion -ne '13.1.0' -or
        $taskCmakeVersion -ne 'cmake version 3.30.5' -or $taskNinjaVersion -ne '1.12.1' -or
        $taskFormatVersion -notmatch '\b19\.1\.7\b') {
        throw '实际工具版本与冻结值不一致，请先核对工具路径。'
    }
    $taskReport.versions = [ordered]@{
        qt = $taskQtVersion; gcc = $taskGccVersion; cmake = $taskCmakeVersion
        ninja = $taskNinjaVersion; clangFormat = $taskFormatVersion
    }
    $taskReport.versions | ConvertTo-Json | Write-Host

    # 自动测试使用无屏幕平台，不据此声称通过了真实鼠标、输入法和高DPI检查。
    $taskPreviousPlatform = $env:QT_QPA_PLATFORM
    $taskPreviousTemp = $env:TEMP
    $taskPreviousTmp = $env:TMP
    $taskTestTemp = Join-Path $taskEvidenceDir 'temp'
    New-Item -ItemType Directory -Path $taskTestTemp -Force | Out-Null
    $env:TEMP = $taskTestTemp
    $env:TMP = $taskTestTemp
    $env:QT_QPA_PLATFORM = 'offscreen'
    try {
        $taskConfigs = if ($Configuration -eq 'Both') { @('debug', 'release') } else { @($Configuration.ToLowerInvariant()) }
        foreach ($taskConfig in $taskConfigs) {
            Invoke-CheckedTool 'cmake' @('--preset', $taskConfig)
            Invoke-CheckedTool 'cmake' @('--build', '--preset', $taskConfig, '--parallel', '2')
            & (Join-Path $PSScriptRoot '准备运行依赖.ps1') -Configuration $taskConfig -ToolsRoot $ToolsRoot
            if (-not $?) { throw '运行依赖准备失败。' }
            Invoke-CheckedTool 'ctest' @('--preset', $taskConfig, '--no-tests=error', '--verbose')
            $taskReport.configurations += [ordered]@{ name = $taskConfig; build = '通过'; tests = '通过' }
        }
    }
    finally {
        if ($null -eq $taskPreviousPlatform) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
        else { $env:QT_QPA_PLATFORM = $taskPreviousPlatform }
        if ($null -eq $taskPreviousTemp) { Remove-Item Env:TEMP -ErrorAction SilentlyContinue }
        else { $env:TEMP = $taskPreviousTemp }
        if ($null -eq $taskPreviousTmp) { Remove-Item Env:TMP -ErrorAction SilentlyContinue }
        else { $env:TMP = $taskPreviousTmp }
    }
    $taskReport.success = $true
    Write-Host '本机冻结版本、配置、构建与测试检查通过；三机一致性须由另外两名成员分别复现。'
}
finally {
    $taskReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $taskReportFile -Encoding utf8
    Stop-Transcript | Out-Null
}
