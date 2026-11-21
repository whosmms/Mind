$ErrorActionPreference='Stop'
$report=Get-Content build/h046-delivery-evidence/validation.json -Raw -Encoding UTF8|ConvertFrom-Json
$product=$report.sourceCommit
if(-not $report.success) {throw '全量验证未成功'}
foreach($cfg in @('debug','release')) {
 $rows=@($report.suites|Where-Object configuration -eq $cfg)
 if($rows.Count -ne 29 -or ($rows|Measure-Object passed -Sum).Sum -ne 598 -or ($rows|Measure-Object failed -Sum).Sum -ne 0 -or ($rows|Measure-Object skipped -Sum).Sum -ne 0) {throw '统计不符'}
}
git diff --quiet $product -- src tests resources CMakeLists.txt CMakePresets.json
if($LASTEXITCODE -ne 0) {throw '验证后源码变化'}
$candidate='D:/mind/候选版本/视觉质检版_20261107_'+$product.Substring(0,7)
if(Test-Path $candidate) {throw '候选已存在，不覆盖'}
$old='D:/mind/候选版本/完整开发版_20261107_59c8381'
$template=Get-Content ($old+'/候选程序校验.json') -Raw -Encoding UTF8|ConvertFrom-Json
New-Item -ItemType Directory -Path ($candidate+'/program'),($candidate+'/说明'),($candidate+'/启动日志')|Out-Null
$files=@()
foreach($entry in $template.files) {
 $source=if($entry.path -eq 'qt.conf') {Join-Path ($old+'/program') $entry.path} else {Join-Path (Resolve-Path build/release).Path $entry.path}
 $destination=Join-Path ($candidate+'/program') $entry.path
 New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force|Out-Null
 Copy-Item -LiteralPath $source -Destination $destination
 $hash=(Get-FileHash $destination).Hash
 if($hash -ne (Get-FileHash $source).Hash) {throw '复制校验失败'}
 $files+=@{path=$entry.path;sha256=$hash}
}
foreach($folder in @('样例','许可证')) {Copy-Item -LiteralPath ($old+'/'+$folder) -Destination $candidate -Recurse}
foreach($name in @('用户使用说明.md','演示顺序.md','最终统一验收.md')) {Copy-Item -LiteralPath ('docs/'+$name) -Destination ($candidate+'/说明')}
Copy-Item -LiteralPath ($old+'/启动本次候选.cmd') -Destination $candidate
Copy-Item -LiteralPath build/h046-delivery-visual -Destination ($candidate+'/截图') -Recurse
Copy-Item -LiteralPath build/h046-delivery-evidence/validation.json -Destination ($candidate+'/说明/自动验证.json')
$fixtures=@(Get-ChildItem ($candidate+'/样例') -Filter '*.lingya' -File|ForEach-Object {@{path=$_.Name;sha256=(Get-FileHash $_.FullName).Hash;origin='H044固定样例副本，非本轮重新构造'}})
$saved=@{}
foreach($key in @('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QT_DEBUG_PLUGINS','QT_ASSUME_STDERR_HAS_CONSOLE','LINGYA_PROFILE_ROOT')) {$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
try {
 $env:PATH=$env:SystemRoot+'\System32;'+$env:SystemRoot
 Remove-Item Env:QT_PLUGIN_PATH,Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
 $env:QT_QPA_PLATFORM='windows'
 $env:QT_DEBUG_PLUGINS='1';$env:QT_ASSUME_STDERR_HAS_CONSOLE='1'
 $env:LINGYA_PROFILE_ROOT=$candidate+'/自动验证资料'
 $own=Start-Process -FilePath ($candidate+'/program/lingya.exe') -WorkingDirectory ($candidate+'/program') -WindowStyle Hidden -PassThru -RedirectStandardError ($candidate+'/启动日志/stderr.log') -RedirectStandardOutput ($candidate+'/启动日志/stdout.log')
 try {
  if($own.WaitForExit(8000)) {throw '程序提前退出'}
  $own.Refresh()
  $modules=@($own.Modules|ForEach-Object {$_.FileName.Replace('\','/')})
  if(-not @($modules|Where-Object {$_ -eq ($candidate+'/program/platforms/qwindows.dll')})) {throw '未加载本包Windows插件'}
  foreach($module in $modules) {
   if(-not $module.StartsWith(($candidate+'/program/'),[StringComparison]::OrdinalIgnoreCase) -and -not $module.StartsWith(($env:SystemRoot.Replace('\','/')+'/'),[StringComparison]::OrdinalIgnoreCase)) {throw '存在包外运行库'}
  }
 } finally {if($own -and -not $own.HasExited) {Stop-Process -Id $own.Id; $null=$own.WaitForExit(3000)}}
} finally {foreach($key in $saved.Keys) {[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}}
@{sourceCommit=$product;configuration='Release';baseMain='faeac4b4e3f8c37235081ff50cef8737e4bd8ae6';files=$files;fixtureHashes=$fixtures;nativeStartup=@{aliveMilliseconds=8000;processModules=$modules;stoppedOnlyOwnProcess=$true};validation='H046：Debug/Release各29套件598通过，0失败0跳过；原生Windows三档缩放专项及页面截图单列，不计入全量。最终真人/跨机/API未代签。'}|ConvertTo-Json -Depth 9|Set-Content ($candidate+'/候选程序校验.json') -Encoding utf8
@{candidate=$candidate;sourceCommit=$product;runtimeFiles=$files.Count;fixtures=$fixtures.Count}|ConvertTo-Json