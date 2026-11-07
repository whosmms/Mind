$ErrorActionPreference = 'Stop'
. ./tools/进入开发环境.ps1 -ToolsRoot D:/mind/.tools
$batchCommit = ((git rev-parse HEAD) -join '').Trim()
$batchRoot = Join-Path (Resolve-Path build).Path 'h047-final-evidence'
New-Item -ItemType Directory -Path $batchRoot -Force | Out-Null
$env:TEMP = (Resolve-Path build/environment/temp).Path
$env:TMP = $env:TEMP
$env:QT_QPA_PLATFORM = 'offscreen'
$batchReport = [ordered]@{ sourceCommit=$batchCommit; dateUtc=[DateTime]::UtcNow.ToString('o'); suites=@(); success=$false }
try {
    foreach ($batchCfg in @('debug','release')) {
        $batchDir = Join-Path $batchRoot $batchCfg
        New-Item -ItemType Directory -Path $batchDir -Force | Out-Null
        cmake --preset $batchCfg *> (Join-Path $batchDir 'configure.log')
        if ($LASTEXITCODE -ne 0) { throw "配置失败 $batchCfg" }
        cmake --build --preset $batchCfg --parallel 2 *> (Join-Path $batchDir 'build.log')
        if ($LASTEXITCODE -ne 0) { throw "构建失败 $batchCfg" }
        ./tools/准备运行依赖.ps1 -Configuration $batchCfg -ToolsRoot D:/mind/.tools *> (Join-Path $batchDir 'deploy.log')
        if (-not $?) { throw "部署失败 $batchCfg" }
        $env:QT_PLUGIN_PATH = (Resolve-Path ('build/'+$batchCfg)).Path
        ctest --preset $batchCfg --no-tests=error --output-on-failure *> (Join-Path $batchDir 'ctest.log')
        if ($LASTEXITCODE -ne 0) { throw "CTest失败 $batchCfg" }
        foreach ($batchExe in (Get-ChildItem ('build/'+$batchCfg+'/tests') -Filter 'tst_*.exe' -File | Sort-Object Name)) {
            $batchXml = Join-Path $batchDir ($batchExe.BaseName+'.xml')
            & $batchExe.FullName -o ($batchXml+',xml') *> (Join-Path $batchDir ($batchExe.BaseName+'.console.log'))
            $batchExit = $LASTEXITCODE
            if (-not (Test-Path -LiteralPath $batchXml)) { throw ('未产生XML：'+$batchExe.Name) }
            [xml]$batchData = Get-Content -LiteralPath $batchXml -Raw
            $batchIncidents = @($batchData.SelectNodes('//Incident'))
            $batchEntry = [pscustomobject]@{ configuration=$batchCfg; name=$batchExe.BaseName; exitCode=$batchExit; passed=@($batchIncidents|Where-Object type -eq pass).Count; failed=@($batchIncidents|Where-Object type -eq fail).Count; skipped=@($batchData.SelectNodes('//*[@type="skip"]')).Count }
            $batchReport.suites += $batchEntry
            if ($batchExit -ne 0 -or $batchEntry.failed -ne 0 -or $batchEntry.passed -eq 0) { throw ('套件失败：'+$batchExe.Name) }
        }
        $batchRows=@($batchReport.suites | Where-Object configuration -eq $batchCfg)
        if ($batchRows.Count -ne 30) { throw '套件数与当前CMake不一致' }
        Write-Output ([pscustomobject]@{ configuration=$batchCfg; suites=$batchRows.Count; passed=($batchRows|Measure-Object passed -Sum).Sum; failed=($batchRows|Measure-Object failed -Sum).Sum; skipped=($batchRows|Measure-Object skipped -Sum).Sum })
    }
    if (((git rev-parse HEAD)-join '').Trim() -ne $batchCommit) { throw '验证期间HEAD改变，不能归为同提交结果' }
    $batchReport.success = $true
} finally { $batchReport | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $batchRoot 'validation.json') -Encoding utf8 }










