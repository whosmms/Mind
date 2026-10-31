$ErrorActionPreference='Stop'
. ./tools/进入开发环境.ps1 -ToolsRoot D:/mind/.tools
$probeRoot=Join-Path (Resolve-Path build).Path 'month-axis-probe'
New-Item -ItemType Directory -Path $probeRoot -Force|Out-Null
$qtRoot='D:/mind/.tools/Qt/6.8.3/mingw_64'
& (Join-Path $qtRoot 'bin/moc.exe') src/charts/GanttView.h -o (Join-Path $probeRoot 'moc_GanttView.cpp')
if($LASTEXITCODE -ne 0){throw 'moc失败'}
$includeFlags=@('-Isrc',('-I'+$qtRoot+'/include'),('-I'+$qtRoot+'/include/QtCore'),('-I'+$qtRoot+'/include/QtGui'),('-I'+$qtRoot+'/include/QtWidgets'))
$commonFlags=@('-std=c++17','-O0','-DQT_CORE_LIB','-DQT_GUI_LIB','-DQT_WIDGETS_LIB','-finput-charset=UTF-8')+$includeFlags
$sources=@('build/month-axis-review-probe.cpp','src/charts/GanttView.cpp','src/charts/GanttLayout.cpp',(Join-Path $probeRoot 'moc_GanttView.cpp'))
$probeExe=Join-Path $probeRoot 'month-axis-probe.exe'
& g++ @commonFlags @sources ('-L'+$qtRoot+'/lib') -lQt6Widgets -lQt6Gui -lQt6Core -o $probeExe *> (Join-Path $probeRoot 'compile.log')
if($LASTEXITCODE -ne 0){throw '评审探针编译失败'}
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_PLUGIN_PATH=(Resolve-Path build/release).Path
foreach($fontDpi in @(96,120,144)){
 $env:QT_FONT_DPI=[string]$fontDpi
 & $probeExe > (Join-Path $probeRoot ('fixed-'+$fontDpi+'.json')) 2> (Join-Path $probeRoot ('fixed-'+$fontDpi+'.stderr.txt'))
 if($LASTEXITCODE -ne 0){throw ('探针失败DPI='+$fontDpi)}
 $data=Get-Content -LiteralPath (Join-Path $probeRoot ('fixed-'+$fontDpi+'.json')) -Raw|ConvertFrom-Json
 [ordered]@{fontDpi=$fontDpi;scenarios=@($data.scenarios).Count;success=$data.success}|ConvertTo-Json
}
$legacySource=Join-Path $probeRoot 'legacy-GanttView.cpp'
git show d9f6211:src/charts/GanttView.cpp | Set-Content -LiteralPath $legacySource -Encoding utf8
& g++ @commonFlags build/month-axis-review-probe.cpp $legacySource src/charts/GanttLayout.cpp (Join-Path $probeRoot 'moc_GanttView.cpp') ('-L'+$qtRoot+'/lib') -lQt6Widgets -lQt6Gui -lQt6Core -o (Join-Path $probeRoot 'legacy-probe.exe') *> (Join-Path $probeRoot 'legacy-compile.log')
if($LASTEXITCODE -ne 0){throw '旧实现探针编译失败'}
$env:QT_FONT_DPI='96'
& (Join-Path $probeRoot 'legacy-probe.exe') --legacy > (Join-Path $probeRoot 'legacy-96.json') 2> (Join-Path $probeRoot 'legacy-96.stderr.txt')
if($LASTEXITCODE -ne 0){throw '旧实现未复现文字重叠'}
$data=Get-Content -LiteralPath (Join-Path $probeRoot 'legacy-96.json') -Raw|ConvertFrom-Json
[ordered]@{legacyExpectedOverlap=$data.success;overlappingScenarios=@($data.scenarios|Where-Object textOverlap).Count}|ConvertTo-Json
