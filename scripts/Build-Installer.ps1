[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PayloadDir,
    [string]$OutputDir,
    [string]$ProjectUrl = 'https://github.com/Ming-Sir-69/Fucairserver',
    [string]$Iscc,
    [switch]$LocalBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
$PayloadDir = [IO.Path]::GetFullPath($PayloadDir)
if (-not $OutputDir) { $OutputDir = Join-Path $projectRoot ('dist/releases/v' + $version) }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if (-not $Iscc) { $Iscc = Join-Path $projectRoot 'tools/inno/ISCC.exe' }
$manifest = Get-Content -LiteralPath (Join-Path $PayloadDir 'release-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.version -ne $version) { throw 'Package VERSION does not match the installer.' }
if (-not $LocalBuild -and (-not $manifest.public_build -or $manifest.proprietary_icon -or (Test-Path -LiteralPath (Join-Path $PayloadDir 'Fucairserver.ico')))) { throw 'Public installers require --public packages without the private icon.' }
if ((Get-Item -LiteralPath (Join-Path $PayloadDir 'Fucairserver.exe')).VersionInfo.ProductVersion -ne $version) { throw 'Executable version does not match VERSION; rebuild first.' }
[IO.Directory]::CreateDirectory($OutputDir) | Out-Null
$arguments = @('-q',('-dProductVersion='+$version),('-dPayloadDir='+$PayloadDir),('-dReleaseDir='+$OutputDir),('-dProjectURL='+$ProjectUrl),(Join-Path $projectRoot 'installer/Fucairserver.iss'))
& $Iscc @arguments
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$installer = Join-Path $OutputDir ('Fucairserver-' + $version + '-windows-x64-setup.exe')
$signature = Get-AuthenticodeSignature -LiteralPath $installer
[pscustomobject]@{ Installer=$installer; Version=$version; Bytes=(Get-Item -LiteralPath $installer).Length; Signature=$signature.Status.ToString(); SHA256=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant() } | ConvertTo-Json
