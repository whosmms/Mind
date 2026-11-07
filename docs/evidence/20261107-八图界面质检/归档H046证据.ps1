$ErrorActionPreference='Stop'
$root='docs/evidence/20261107-八图界面质检'
$report=Get-Content build/h046-delivery-evidence/validation.json -Raw | ConvertFrom-Json
$pages=Get-Content build/h046-delivery-visual/pages-validation.json -Raw | ConvertFrom-Json
$scales=@(Get-Content build/h046-delivery-visual/scale-validation.json -Raw | ConvertFrom-Json)
if(-not $report.success -or $pages.exitCode -ne 0 -or $pages.passed -ne 125 -or $scales.Count -ne 3 -or @($scales|Where-Object { $_.exitCode -ne 0 -or $_.failed -ne 0 }).Count) {throw '交付核查未完成'}
Copy-Item ($root+'/validation.json') ($root+'/开发中失败/3959f21-早期全绿.json')
foreach($cfg in @('debug','release')) {
 Copy-Item ('build/h046-delivery-evidence/'+$cfg+'/*.xml') ($root+'/'+$cfg) -Force
 foreach($log in @('configure','build','deploy','ctest')) {Copy-Item ('build/h046-delivery-evidence/'+$cfg+'/'+$log+'.log') ($root+'/'+$cfg+'/'+$log+'.txt') -Force}
}
Copy-Item build/h046-delivery-evidence/validation.json ($root+'/validation.json') -Force
New-Item -ItemType Directory -Path ($root+'/页面截图'),($root+'/原生倍率') -Force | Out-Null
Copy-Item build/h046-delivery-visual/full-pages/*.png ($root+'/页面截图')
Copy-Item build/h046-delivery-visual/full-pages/all-ui.xml ($root+'/页面截图')
Copy-Item build/h046-delivery-visual/pages-validation.json ($root+'/页面截图')
Copy-Item build/h046-delivery-visual/scale-validation.json ($root+'/原生倍率')
foreach($scale in @('1','1.5','2')) {
 foreach($name in @('controls.xml','console.txt','exit-code.txt')) {Copy-Item ('build/h046-delivery-visual/windows-scale-'+$scale+'/'+$name) ($root+'/原生倍率/'+$scale+'-'+$name)}
}
Copy-Item build/h046-visual-audit/windows-scale-2/controls.xml ($root+'/开发中失败/3959f21-首次倍率2-截断.xml')
foreach($name in @('h046-scale2-retry.xml','h046-scale2-sequence-retry.xml','h046-scale2-exit.txt','h046-scale2-sequence-exit.txt','h046-scale2-debugger2.txt','h046-software-probe.txt')) {Copy-Item ('build/'+$name) ($root+'/开发中失败')}
Copy-Item build/h046-release-evidence/validation.json ($root+'/开发中失败/中途源变更-拒绝归档.json')
foreach($name in @('验证H046交付最终.ps1','截图H046交付最终.ps1','归档H046候选.ps1','归档H046证据.ps1')) {Copy-Item ('build/'+$name) $root -Force}
$report.sourceCommit
