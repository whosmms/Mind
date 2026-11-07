$ErrorActionPreference='Stop'
. ./tools/进入开发环境.ps1 -ToolsRoot D:/mind/.tools
$source=((git rev-parse HEAD)-join '').Trim()
$root=Join-Path (Resolve-Path build).Path 'h048-final5-visual'
if(Test-Path -LiteralPath $root){throw '不覆盖旧截图证据'}
New-Item -ItemType Directory -Path $root|Out-Null
$env:TEMP=(Resolve-Path build/environment/temp).Path
$env:TMP=$env:TEMP
$env:QT_PLUGIN_PATH=(Resolve-Path build/release).Path
$cases=@('freeTreeMouseAndPlus','flowCurveMouseReconnect','instantStyleWheelAndBrand','embeddedImagesAcrossViews','templateGalleryAndAiTutorial','quickMouseRelationsGroupsAndImagePicker')
$report=@()
foreach($mode in @('offscreen','windows-1','windows-1.5','windows-2')){
 $folder=Join-Path $root $mode
 New-Item -ItemType Directory -Path $folder|Out-Null
 if($mode -eq 'offscreen'){$env:QT_QPA_PLATFORM='offscreen';$env:QT_SCALE_FACTOR='1';$env:LINGYA_INTERACTION_CAPTURE_DIR=$folder}
 else{$env:QT_QPA_PLATFORM='windows';$env:QT_SCALE_FACTOR=$mode.Substring(8);Remove-Item Env:LINGYA_INTERACTION_CAPTURE_DIR -ErrorAction SilentlyContinue}
 & ./build/release/tests/tst_ui.exe @cases -o ($folder+'/interaction.xml,xml') *> ($folder+'/console.log')
 $exit=$LASTEXITCODE
 [xml]$xml=Get-Content -LiteralPath ($folder+'/interaction.xml') -Raw
 $row=[ordered]@{sourceCommit=$source;mode=$mode;exitCode=$exit;passed=@($xml.SelectNodes('//Incident[@type="pass"]')).Count;failed=@($xml.SelectNodes('//Incident[@type="fail"]')).Count;skipped=@($xml.SelectNodes('//*[@type="skip"]')).Count;screenshots=@(Get-ChildItem $folder -Filter '*.png').Count}
 $report+=$row
 $report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath ($root+'/visual-validation.json') -Encoding utf8
 if($exit -ne 0 -or $row.failed -ne 0 -or $row.skipped -ne 0){throw ('截图/原生鼠标专项失败：'+$mode)}
 Write-Output ([pscustomobject]$row)
}
if(((git rev-parse HEAD)-join '').Trim() -ne $source){throw '截图期间HEAD改变'}


