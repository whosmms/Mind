[CmdletBinding()]
param(
    [string]$PointerFile = '',
    [string]$ProjectFile = '',
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
function Get-CandidateHash([string]$File) {
    $algorithm=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($File)
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','') }
    finally {$stream.Dispose();$algorithm.Dispose()}
}
if (-not $PointerFile) {$PointerFile=Join-Path $PSScriptRoot '当前开发版.json'}
$pointer=Get-Content -LiteralPath $PointerFile -Raw -Encoding UTF8|ConvertFrom-Json
$candidate=[IO.Path]::GetFullPath($pointer.candidatePath)
if (-not $candidate.StartsWith('D:\mind\候选版本\',[StringComparison]::OrdinalIgnoreCase)) {throw '开发候选指针不在约定目录内'}
$manifestPath=Join-Path $candidate '候选程序校验.json'
if ((Get-CandidateHash $manifestPath) -ne $pointer.manifestSha256) {throw '候选说明已改变，请先重新同步开发版指针'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8|ConvertFrom-Json
if ($manifest.sourceCommit -ne $pointer.sourceCommit) {throw '开发版源码指针不一致'}
$program=Join-Path $candidate 'program'
foreach ($entry in $manifest.files) {
    $file=[IO.Path]::GetFullPath((Join-Path $program $entry.path))
    if (-not $file.StartsWith(($program+'\'),[StringComparison]::OrdinalIgnoreCase)) {throw '候选文件路径越界'}
    for ($ancestor=$file; $ancestor; $ancestor=Split-Path $ancestor -Parent) {
        if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw '候选文件路径经过链接'}
    }
    if ((Get-CandidateHash $file) -ne $entry.sha256) {throw ('候选运行文件损坏：'+$entry.path)}
}
if ($ValidateOnly) {
    Write-Output ([pscustomobject]@{sourceCommit=$manifest.sourceCommit;runtimeFilesVerified=$manifest.files.Count;candidate=$candidate;pullRequest=$pointer.pullRequest})
    return
}
$arguments=@()
if ($ProjectFile) {
    $project=(Resolve-Path -LiteralPath $ProjectFile).Path
    $arguments=@('"'+$project+'"')
}
Write-Output ('正在启动最新开发候选 '+$manifest.sourceCommit.Substring(0,7)+'；'+$pointer.pullRequest)
$startParameters=@{FilePath=(Join-Path $program 'lingya.exe');WorkingDirectory=$program;WindowStyle='Normal'}
if ($arguments.Count) {$startParameters.ArgumentList=$arguments}
Start-Process @startParameters
