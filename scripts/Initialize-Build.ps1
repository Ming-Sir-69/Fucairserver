# Requires PowerShell 7.4+, Node.js 22+, Git and a recent MSYS2 base installation.
[CmdletBinding()]
param(
    [string]$MsysRoot = $env:FUCAIRSERVER_MSYS_ROOT,
    [string]$Proxy = $env:FUCAIRSERVER_PROXY,
    [switch]$InstallPackages,
    [switch]$IncludeMiracast,
    [switch]$IncludeInstaller
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $MsysRoot) { $MsysRoot = Join-Path $projectRoot 'tools/msys64' }
$MsysRoot = [IO.Path]::GetFullPath($MsysRoot)
$env:FUCAIRSERVER_MSYS_ROOT = $MsysRoot
if ($Proxy) { $env:FUCAIRSERVER_PROXY = $Proxy; $env:HTTP_PROXY = $Proxy; $env:HTTPS_PROXY = $Proxy }
$downloads = Join-Path $projectRoot 'build/downloads'
[IO.Directory]::CreateDirectory($downloads) | Out-Null
function Get-LockedDownload([string]$Url, [string]$Path, [string]$Sha256) {
    if (-not (Test-Path -LiteralPath $Path)) {
        $curlArguments = @('--fail','--location','--retry','3','--connect-timeout','20','--max-time','300','--silent','--show-error','--output',$Path,$Url)
        if ($Proxy) { $curlArguments = @('--proxy',$Proxy) + $curlArguments }
        & curl.exe @curlArguments
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
    }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha256) { throw "SHA-256 mismatch: $Path. Move the incomplete file out of the download directory and retry." }
}
$lock = Get-Content -LiteralPath (Join-Path $projectRoot 'profiles/toolchain-lock.json') -Raw | ConvertFrom-Json
if ($InstallPackages) {
    $pacman = Join-Path $MsysRoot 'usr/bin/pacman.exe'
    if (-not (Test-Path -LiteralPath $pacman)) { throw 'Install the official MSYS2 base first, then specify -MsysRoot.' }
    $packagePaths = foreach ($package in $lock.packages) {
        $path = Join-Path $downloads $package.filename
        Get-LockedDownload $package.url $path $package.sha256
        $path.Replace('\','/')
    }
    & $pacman --root ($MsysRoot.Replace('\','/')) --dbpath ((Join-Path $MsysRoot 'var/lib/pacman').Replace('\','/')) --cachedir ($downloads.Replace('\','/')) -U --noconfirm --needed @packagePaths
    if ($LASTEXITCODE -ne 0) { throw 'Installing the pinned MSYS2 packages failed.' }
}
foreach ($package in $lock.packages) {
    if (-not (Test-Path -LiteralPath (Join-Path $MsysRoot ('var/lib/pacman/local/' + $package.name + '-' + $package.version + '/desc')))) { throw "Pinned package is missing: $($package.name) $($package.version); rerun with -InstallPackages." }
}
$toolLock = Get-Content -LiteralPath (Join-Path $projectRoot 'profiles/build-tools.json') -Raw | ConvertFrom-Json
if ($IncludeMiracast) {
    $archive = Join-Path $downloads ('Microsoft.Windows.CppWinRT.' + $toolLock.cppwinrt.version + '.nupkg')
    Get-LockedDownload $toolLock.cppwinrt.url $archive $toolLock.cppwinrt.sha256
    $destination = Join-Path $projectRoot 'third_party/miracast-reference/research/cppwinrt-tool'
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination, $true)
}
if ($IncludeInstaller) {
    $archive = Join-Path $downloads ('innosetup-' + $toolLock.inno_setup.version + '-x64.exe')
    Get-LockedDownload $toolLock.inno_setup.url $archive $toolLock.inno_setup.sha256
    if ((Get-AuthenticodeSignature -LiteralPath $archive).Status -ne 'Valid') { throw 'The downloaded Inno Setup executable has an invalid Authenticode signature.' }
    $innoRoot = Join-Path $projectRoot 'tools/inno'
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER',('/DIR="' + $innoRoot + '"'),'/NOICONS','/TASKS=')
    $process = Start-Process -FilePath $archive -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "Inno Setup install failed: $($process.ExitCode)" }
}
& (Join-Path $PSScriptRoot 'Fetch-Source.ps1') -Proxy $Proxy
Write-Output 'Pinned build dependencies are ready.'
