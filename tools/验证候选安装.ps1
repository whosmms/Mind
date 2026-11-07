# 对本轮安装包在D:/mind隔离目录执行真实安装/重装/启动/卸载；不碰现有软件。
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SetupFile,
    [Parameter(Mandatory)][string]$PayloadDirectory,
    [Parameter(Mandatory)][string]$TestRoot,
    [string]$PortableArchive = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($TestRoot)
function Assert-LocalPath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith('D:\mind\',[StringComparison]::OrdinalIgnoreCase)) {throw '自动安装验证只允许D:/mind内路径'}
    for ($ancestor=$absolute; $ancestor; $ancestor=Split-Path $ancestor -Parent) {
        if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw '验证路径不能经过链接'}
    }
}
Assert-LocalPath $root
Assert-LocalPath $SetupFile
Assert-LocalPath $PayloadDirectory
if (Test-Path -LiteralPath $root) {throw '隔离验证目录已存在，禁止覆盖证据'}
New-Item -ItemType Directory -Path $root | Out-Null
$saved=@{}
foreach ($key in @('TEMP','TMP','PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_QPA_PLATFORM','QT_DEBUG_PLUGINS','QT_ASSUME_STDERR_HAS_CONSOLE','LINGYA_PROFILE_ROOT')) { $saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process') }
$report=[ordered]@{ dateUtc=[DateTime]::UtcNow.ToString('o'); setupSha256=(Get-FileHash -LiteralPath $SetupFile -Algorithm SHA256).Hash; root=$root; success=$false }
function Run-Setup([string]$Directory,[string]$LogName) {
    $arguments=@('/SANDBOX=1','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/NOICONS','/NOCLOSEAPPLICATIONS',('/DIR="'+$Directory+'"'),('/LOG="'+(Join-Path $root $LogName)+'"'))
    $process=Start-Process -FilePath $SetupFile -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) {throw '安装器未按时结束，保留现场不终止其他进程'}
    return $process.ExitCode
}
try {
    $temporary=Join-Path $root 'temp'
    New-Item -ItemType Directory -Path $temporary | Out-Null
    $env:TEMP=$temporary; $env:TMP=$temporary
    $foreign=Join-Path $root '非空陌生目录'
    New-Item -ItemType Directory -Path $foreign | Out-Null
    $sentinel=Join-Path $foreign '不得覆盖.lingya'
    [IO.File]::WriteAllText($sentinel,'本轮自动创建的非工程哨兵文件')
    $sentinelHash=(Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
    $blocked=Run-Setup $foreign 'blocked.log'
    if ($blocked -eq 0 -or (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -ne $sentinelHash -or @(Get-ChildItem -LiteralPath $foreign -File -Recurse).Count -ne 1) {throw '安装器未能保护陌生非空目录'}
    $report.blockedForeignDirectory=$true
    $report.blockedExitCode=$blocked
    $installed=Join-Path $root '已安装候选'
    if ((Run-Setup $installed 'install.log') -ne 0) {throw '隔离安装失败'}
    $payload=(Resolve-Path -LiteralPath $PayloadDirectory).Path
    $files=@(Get-ChildItem -LiteralPath $payload -File -Recurse)
    foreach ($file in $files) {
        $destination=Join-Path $installed $file.FullName.Substring($payload.Length+1)
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {throw ('安装后文件不一致：'+$file.Name)}
    }
    $report.installedFilesVerified=$files.Count
    $kept=Join-Path $installed '自行添加的工程.lingya'
    [IO.File]::WriteAllText($kept,'本轮自动创建，验证重装及卸载不删未知用户文件')
    $keptHash=(Get-FileHash -LiteralPath $kept -Algorithm SHA256).Hash
    if ((Run-Setup $installed 'reinstall.log') -ne 0 -or (Get-FileHash -LiteralPath $kept -Algorithm SHA256).Hash -ne $keptHash) {throw '原目录重装未能保留未知文件'}
    $report.reinstallPreservedUnknownFile=$true
    $env:PATH=$env:SystemRoot+'\System32;'+$env:SystemRoot
    Remove-Item Env:QT_PLUGIN_PATH,Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
    $env:QT_QPA_PLATFORM='windows'; $env:QT_DEBUG_PLUGINS='1'; $env:QT_ASSUME_STDERR_HAS_CONSOLE='1'
    $env:LINGYA_PROFILE_ROOT=Join-Path $root '隔离资料'
    New-Item -ItemType Directory -Path $env:LINGYA_PROFILE_ROOT | Out-Null
    $program=Join-Path $installed 'program'
    $ownProcess=$null
    try {
        $ownProcess=Start-Process -FilePath (Join-Path $program 'lingya.exe') -WorkingDirectory $program -WindowStyle Hidden -PassThru -RedirectStandardError (Join-Path $root 'native-stderr.log') -RedirectStandardOutput (Join-Path $root 'native-stdout.log')
        if ($ownProcess.WaitForExit(8000)) {throw '安装后的程序提前退出'}
        $ownProcess.Refresh()
        $modules=@($ownProcess.Modules | ForEach-Object {$_.FileName})
        $platform=[IO.Path]::GetFullPath((Join-Path $program 'platforms/qwindows.dll'))
        if (-not @($modules|Where-Object {$_ -ieq $platform}).Count) {throw '安装后的程序没有实际加载本包qwindows'}
        foreach ($module in @($modules|Where-Object { [IO.Path]::GetFileName($_) -like 'Qt6*.dll' })) {
            if (-not $module.StartsWith(($program+'\'),[StringComparison]::OrdinalIgnoreCase)) {throw '安装后的Qt库回落到包外'}
        }
        $report.nativeStartup=[ordered]@{aliveMilliseconds=8000;modules=$modules;profileRoot=$env:LINGYA_PROFILE_ROOT}
    } finally {
        if ($ownProcess -and -not $ownProcess.HasExited) {Stop-Process -Id $ownProcess.Id -ErrorAction Stop; $null=$ownProcess.WaitForExit(3000)}
    }
    $uninstallers=@(Get-ChildItem -LiteralPath $installed -Filter 'unins*.exe' -File)
    if ($uninstallers.Count -ne 1) {throw '卸载器数量不符'}
    Assert-LocalPath $uninstallers[0].FullName
    $uninstall=Start-Process -FilePath $uninstallers[0].FullName -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $root 'uninstall.log')+'"')) -WindowStyle Hidden -PassThru
    if (-not $uninstall.WaitForExit(60000) -or $uninstall.ExitCode -ne 0) {throw '隔离卸载失败'}
    if (Test-Path -LiteralPath (Join-Path $program 'lingya.exe')) {throw '卸载后仍存在程序'}
    if ((Get-FileHash -LiteralPath $kept -Algorithm SHA256).Hash -ne $keptHash) {throw '卸载删除了未知用户文件'}
    $report.uninstallPreservedUnknownFile=$true
    if ($PortableArchive) {
        Assert-LocalPath $PortableArchive
        $portable=Join-Path $root '便携包解压'
        Expand-Archive -LiteralPath $PortableArchive -DestinationPath $portable
        foreach ($file in $files) {
            $destination=Join-Path $portable $file.FullName.Substring($payload.Length+1)
            if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {throw '便携ZIP解压内容不一致'}
        }
        $report.portableFilesVerified=$files.Count
    }
    $report.success=$true
} finally {
    foreach ($key in $saved.Keys) {[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root '安装验证.json') -Encoding utf8
}
Write-Output ([pscustomobject]@{success=$report.success;installedFilesVerified=$report.installedFilesVerified;portableFilesVerified=$report.portableFilesVerified;root=$root})
