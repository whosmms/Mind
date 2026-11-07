# 封装已经自动验证的本机候选；不会创建GitHub发布或替换main程序。
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CandidateDirectory,
    [Parameter(Mandatory)][string]$ValidationReport,
    [string]$Compiler = 'D:/mind/.tools/InnoSetup-6.7.4/ISCC.exe'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$candidate = [IO.Path]::GetFullPath($CandidateDirectory)
if (-not $candidate.StartsWith('D:\mind\候选版本\', [StringComparison]::OrdinalIgnoreCase)) { throw '候选必须位于D:/mind/候选版本内' }
for ($item = $candidate; $item; $item = Split-Path $item -Parent) {
    if ((Test-Path -LiteralPath $item) -and ((Get-Item -LiteralPath $item -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw '候选路径不能经过链接' }
}
$report = Get-Content -LiteralPath $ValidationReport -Raw -Encoding UTF8 | ConvertFrom-Json
$manifest = Get-Content -LiteralPath (Join-Path $candidate '候选程序校验.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $report.success -or $manifest.sourceCommit -ne $report.sourceCommit) { throw '候选与成功验证报告的源码不一致' }
foreach ($configuration in @('debug','release')) {
    $rows = @($report.suites | Where-Object configuration -eq $configuration)
    if (-not $rows.Count -or ($rows | Measure-Object failed -Sum).Sum -ne 0 -or ($rows | Measure-Object skipped -Sum).Sum -ne 0) { throw '两配置验证未完成或仍有失败/跳过' }
}
$critical = @('src','tests','resources','CMakeLists.txt','CMakePresets.json')
git -C $repo diff --quiet $report.sourceCommit HEAD -- @critical
if ($LASTEXITCODE -ne 0) { throw '验证后产品源码发生变化，需先重新验证' }
git -C $repo diff --quiet HEAD -- @critical
if ($LASTEXITCODE -ne 0) { throw '存在未冻结的产品源码修改' }
foreach ($entry in $manifest.files) {
    $relative = $entry.path.Replace('\','/')
    if ($relative -match '(^/|:|(^|/)\.\.(/|$))') { throw '运行文件清单路径越界' }
    $file = Join-Path (Join-Path $candidate 'program') $relative
    if ((Get-Item -LiteralPath $file -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw '运行文件不能是链接' }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) { throw ('运行文件校验失败：' + $relative) }
}
$compiler = (Resolve-Path -LiteralPath $Compiler).Path
$compilerSource = Get-Content -LiteralPath (Join-Path (Split-Path $compiler -Parent) '工具来源.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($compilerSource.version -ne '6.7.4' -or (Get-AuthenticodeSignature -LiteralPath $compiler).Status -ne 'Valid') { throw '安装器编译器必须为已登记并核对签名的6.7.4' }
if (@(Get-ChildItem -LiteralPath $candidate -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw '分发源目录不能含链接' }
if (@(Get-ChildItem -LiteralPath (Join-Path $candidate 'program') -Recurse -File).Count -ne $manifest.files.Count) { throw '程序目录有未登记文件' }
$payload = Join-Path $candidate '分发内容'
$output = Join-Path $candidate '安装包与便携包'
if ((Test-Path -LiteralPath $payload) -or (Test-Path -LiteralPath $output)) { throw '分发目录已经存在，不覆盖旧交付' }
New-Item -ItemType Directory -Path $payload,$output | Out-Null
foreach ($name in @('program','样例','说明','许可证')) {
    Copy-Item -LiteralPath (Join-Path $candidate $name) -Destination $payload -Recurse
}
Copy-Item -LiteralPath (Join-Path $candidate '候选程序校验.json') -Destination $payload
$bundleCommit = ((git -C $repo rev-parse HEAD) -join '').Trim()
$sourcePaths = @('CMakeLists.txt','CMakePresets.json','AGENTS.md','README.md','.clang-format','.editorconfig','.gitattributes','.gitignore','src','resources','tests','tools','packaging','docs/设计与实施规范_AI版.md','docs/三人协作开发统一规范.md','docs/用户使用说明.md','docs/最终统一验收.md','docs/演示顺序.md','docs/third-party.md')
$sourceZip = Join-Path $payload ('源码与构建资料-' + $bundleCommit.Substring(0,7) + '.zip')
git -C $repo archive --format=zip ('--output=' + $sourceZip) $bundleCommit -- @sourcePaths
if ($LASTEXITCODE -ne 0) { throw '源码归档失败' }
$files = @(Get-ChildItem -LiteralPath $payload -File -Recurse | Sort-Object FullName | ForEach-Object {
    if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw '分发文件不能是链接' }
    [ordered]@{ path=$_.FullName.Substring($payload.Length+1).Replace('\','/'); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
[ordered]@{ sourceCommit=$report.sourceCommit; packagingCommit=$bundleCommit; files=$files } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $payload '分发文件校验.json') -Encoding utf8
$portable = Join-Path $output ('LingyaMind-Dev-' + $report.sourceCommit.Substring(0,7) + '-Portable.zip')
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $portable -CompressionLevel Optimal
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
try {
    $temporary = Join-Path $candidate '安装器临时目录'
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $env:TEMP=$temporary; $env:TMP=$temporary
    & $compiler ('/DStagingDir=' + $payload) ('/DOutputDir=' + $output) ('/DSourceCommit=' + $report.sourceCommit) (Join-Path $repo 'packaging/lingya.iss') *> (Join-Path $candidate '安装器编译.log')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup编译失败，查看候选内安装器编译日志' }
} finally { $env:TEMP=$oldTemp; $env:TMP=$oldTmp }
$setup = Join-Path $output ('LingyaMind-Dev-' + $report.sourceCommit.Substring(0,7) + '-Setup.exe')
$packages = @($portable,$setup | ForEach-Object { [ordered]@{ path=(Split-Path $_ -Leaf); bytes=(Get-Item -LiteralPath $_).Length; sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } })
[ordered]@{ sourceCommit=$report.sourceCommit; packagingCommit=$bundleCommit; compilerVersion='6.7.4'; packages=$packages; published=$false; installationValidated=$false } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $output '安装包校验.json') -Encoding utf8
Write-Output ([pscustomobject]@{ sourceCommit=$report.sourceCommit; packages=$packages.Count; output=$output; published=$false })
