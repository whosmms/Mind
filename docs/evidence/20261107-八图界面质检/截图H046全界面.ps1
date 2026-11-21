$ErrorActionPreference='Stop'
. ./tools/进入开发环境.ps1 -ToolsRoot D:/mind/.tools
$root=Join-Path (Resolve-Path build).Path 'h046-visual-audit'
New-Item -ItemType Directory -Path $root -Force | Out-Null
$env:TEMP=(Resolve-Path build/environment/temp).Path
$env:TMP=$env:TEMP
$env:QT_PLUGIN_PATH=(Resolve-Path build/release).Path
$summary=@()
foreach($scale in @('1','1.5','2')) {
    $folder=Join-Path $root ('windows-scale-'+$scale)
    if(Test-Path $folder) {throw '不覆盖既有截图'}
    New-Item -ItemType Directory -Path $folder | Out-Null
    $env:QT_QPA_PLATFORM='windows'
    $env:QT_SCALE_FACTOR=$scale
    $env:LINGYA_VISUAL_QA_DIR=$folder
    & ./build/release/tests/tst_ui.exe visualControlsStayInsideBounds -o ($folder+'/controls.xml,xml') *> ($folder+'/console.txt')
    $testExit=$LASTEXITCODE
    [xml]$xml=Get-Content ($folder+'/controls.xml') -Raw
    $row=[ordered]@{platform='windows';requestedScale=$scale;exitCode=$testExit;passed=@($xml.SelectNodes('//Incident[@type="pass"]')).Count;failed=@($xml.SelectNodes('//Incident[@type="fail"]')).Count;skipped=@($xml.SelectNodes('//*[@type="skip"]')).Count;screenshots=@(Get-ChildItem $folder -Filter '*.png').Count}
    $summary+=$row
    $summary | ConvertTo-Json -Depth 8 | Set-Content ($root+'/scale-validation.json') -Encoding utf8
    if($testExit -ne 0 -or $row.failed) {throw ('缩放专项失败：'+$scale)}
}
Remove-Item Env:LINGYA_VISUAL_QA_DIR
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_SCALE_FACTOR='1'
$folder=Join-Path $root 'full-pages'
New-Item -ItemType Directory -Path $folder | Out-Null
foreach($name in @('LINGYA_CHROME_CAPTURE_DIR','LINGYA_UI_DETAIL_CAPTURE_DIR','LINGYA_COMPLETION_CAPTURE_DIR','LINGYA_AI_CAPTURE_DIR','LINGYA_FINAL_CAPTURE_DIR')) {[Environment]::SetEnvironmentVariable($name,$folder,'Process')}
$env:LINGYA_REVIEW_CAPTURE=$folder+'/11-复习卡片.png'
$env:LINGYA_TASK_CAPTURE=$folder+'/12-任务属性.png'
$env:LINGYA_PROPERTIES_CAPTURE=$folder+'/13-主题属性.png'
$env:LINGYA_LOGIN_CAPTURE=$folder+'/14-登录.png'
$env:LINGYA_TAGS_DIALOG_CAPTURE=$folder+'/15-标签.png'
$env:LINGYA_STYLE_DIALOG_CAPTURE=$folder+'/16-样式窗口.png'
& ./build/release/tests/tst_ui.exe -o ($folder+'/all-ui.xml,xml') *> ($folder+'/console.txt')
$testExit=$LASTEXITCODE
[xml]$xml=Get-Content ($folder+'/all-ui.xml') -Raw
[ordered]@{sourceCommit=((git rev-parse HEAD)-join '').Trim();platform='offscreen';exitCode=$testExit;passed=@($xml.SelectNodes('//Incident[@type="pass"]')).Count;failed=@($xml.SelectNodes('//Incident[@type="fail"]')).Count;skipped=@($xml.SelectNodes('//*[@type="skip"]')).Count;screenshots=@(Get-ChildItem $folder -Filter '*.png').Count}|ConvertTo-Json|Set-Content ($root+'/pages-validation.json') -Encoding utf8
if($testExit -ne 0) {throw '全页面截图专项失败'}