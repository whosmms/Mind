# 从官方固定版本准备便携编译器，不注册文件关联、不建立系统快捷方式。
[CmdletBinding()]
param([string]$ToolsRoot = 'D:/mind/.tools')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($ToolsRoot)
if (-not $root.StartsWith('D:\mind\', [StringComparison]::OrdinalIgnoreCase)) { throw '工具目录必须在D:/mind内' }
for ($item = $root; $item; $item = Split-Path $item -Parent) {
    if ((Test-Path -LiteralPath $item) -and ((Get-Item -LiteralPath $item -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw '工具路径不能经过链接' }
}
$version = '6.7.4'
$target = Join-Path $root ('InnoSetup-' + $version)
$compiler = Join-Path $target 'ISCC.exe'
if (Test-Path -LiteralPath $compiler -PathType Leaf) { Write-Output $compiler; return }
if (Test-Path -LiteralPath $target) { throw '发现未完成工具目录，请先核查，不覆盖已有内容' }
$cache = Join-Path $root 'download-cache'
$temporary = Join-Path $root 'temp/inno-6.7.4'
New-Item -ItemType Directory -Path $cache, $temporary -Force | Out-Null
$installer = Join-Path $cache 'innosetup-6.7.4.exe'
$url = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-6.7.4.exe'
$expected = 'A549929319F9120716677C685535CA4671E4B937B5FDC87E2585F32D8EB30E8C'
if (-not (Test-Path -LiteralPath $installer)) { Invoke-WebRequest -Uri $url -OutFile $installer }
if ((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash -ne $expected) { throw '官方安装器SHA256不符，拒绝运行' }
$signature = Get-AuthenticodeSignature -LiteralPath $installer
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Pyrsys B.V.') { throw '官方签名未能核实，拒绝运行' }
$savedTemp = $env:TEMP
$savedTmp = $env:TMP
try {
    $env:TEMP = $temporary
    $env:TMP = $temporary
    $arguments = @('/PORTABLE=1','/CURRENTUSER','/VERYSILENT','/SUPPRESSMSGBOXES','/SP-','/NORESTART','/NOICONS',('/DIR="' + $target + '"'),('/LOG="' + (Join-Path $temporary 'prepare.log') + '"'))
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw '便携安装器准备失败，请查看隔离目录日志' }
    [ordered]@{ version=$version; url=$url; sha256=$expected; signer=$signature.SignerCertificate.Subject; portable=$true; path=$target } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target '工具来源.json') -Encoding utf8
} finally { $env:TEMP = $savedTemp; $env:TMP = $savedTmp }
Write-Output $compiler
