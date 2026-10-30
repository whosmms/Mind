# 一键跑全量测试并逐套件计数（规范§19.1）。
# 用法：powershell -ExecutionPolicy Bypass -File tools\跑测试.ps1 [-Configuration Both] [-Filter <名>]
# 关键点：平台插件与插件根目录在这里显式指定，避免 Qt 找不到插件时弹「no Qt platform plugin」
# 模态框并把进程挂死——那种情况下命令行没有任何输出，看起来像“卡住”。
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'Both')]
    [string]$Configuration = 'Debug',
    [string]$Filter = ''
)

$ErrorActionPreference = 'Stop'
$testRepo = Split-Path $PSScriptRoot -Parent

# 复用统一的工具链定位与冻结环境（同一个 PowerShell 进程内，环境变量会保留）
. (Join-Path $PSScriptRoot '进入开发环境.ps1')

$testConfigs = if ($Configuration -eq 'Both') { @('debug', 'release') } else { @($Configuration.ToLowerInvariant()) }
$testAllPassed = $true

foreach ($testConfig in $testConfigs) {
    $testDir = Join-Path $testRepo ('build\' + $testConfig)
    if (-not (Test-Path -LiteralPath $testDir -PathType Container)) {
        Write-Host "跳过 ${testConfig}：尚未构建（先跑 cmake --preset ${testConfig} 与 cmake --build --preset ${testConfig}）"
        continue
    }
    Write-Host ''
    Write-Host "===== ${testConfig} ====="

    # 插件根目录指向构建目录（里面有 platforms\），平台固定 offscreen：前台不会出现真窗口
    $env:QT_PLUGIN_PATH = $testDir
    $env:QT_QPA_PLATFORM = 'offscreen'

    Push-Location $testDir
    try {
        $testCtestArgs = @('--output-on-failure')
        if ($Filter -ne '') { $testCtestArgs += @('-R', $Filter) }
        $testCtestOutput = & ctest @testCtestArgs 2>&1
        $testCtestCode = $LASTEXITCODE
        ($testCtestOutput | Select-Object -Last 3) | ForEach-Object { Write-Host "  ctest: $_" }
        if ($testCtestCode -ne 0) { $testAllPassed = $false }

        Write-Host '  逐套件计数（每个 exe 单独跑一遍）：'
        $testTotal = 0
        $testXml = Join-Path $testDir ('_count_' + [Guid]::NewGuid().ToString('N') + '.xml')
        foreach ($testExe in (Get-ChildItem -LiteralPath (Join-Path $testDir 'tests') -Filter 'tst_*.exe' -File | Sort-Object Name)) {
            $testName = [IO.Path]::GetFileNameWithoutExtension($testExe.Name)
            if ($Filter -ne '' -and $testName -notmatch $Filter) { continue }
            & $testExe.FullName ('-o', ($testXml + ',xml')) 2>&1 | Out-Null
            $testCode = $LASTEXITCODE
            $testPass = 0
            $testFail = 0
            if (Test-Path -LiteralPath $testXml -PathType Leaf) {
                $testPass = (Select-String -LiteralPath $testXml -Pattern 'Incident type="pass"' -AllMatches).Count
                $testFail = (Select-String -LiteralPath $testXml -Pattern 'Incident type="fail"' -AllMatches).Count
                Remove-Item -LiteralPath $testXml -Force
            }
            $testTotal += $testPass
            $testMark = if ($testCode -eq 0 -and $testFail -eq 0) { 'ok  ' } else { 'FAIL' }
            if ($testMark -eq 'FAIL') { $testAllPassed = $false }
            Write-Host ("  {0} {1,-20} pass={2,-3} fail={3}" -f $testMark, $testName, $testPass, $testFail)
        }
        Write-Host "  ${testConfig} 合计通过 $testTotal 项"
    } finally {
        Pop-Location
    }
}

Write-Host ''
if ($testAllPassed) { Write-Host '全部通过。' } else { Write-Host '存在失败项，详见上面的输出。' }
if (-not $testAllPassed) { exit 1 }
