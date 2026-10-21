[CmdletBinding()]
param([switch]$PublicBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw 'VERSION must contain major.minor.patch' }
$parts = @($version.Split('.') | ForEach-Object { [int]$_ })
if (@($parts | Where-Object { $_ -gt 65535 }).Count) { throw 'Windows version components must not exceed 65535' }
$hasIcon = if (-not $PublicBuild -and (Test-Path -LiteralPath (Join-Path $projectRoot 'assets/Fucairserver.ico'))) { 1 } else { 0 }
$header = "#pragma once`n#define FUCAIRSERVER_VERSION `"$version`"`n#define FUCAIRSERVER_VERSION_RESOURCE $($parts -join ','),0`n#define FUCAIRSERVER_HAS_ICON $hasIcon`n"
$path = Join-Path $projectRoot 'src/version.generated.h'
if (-not (Test-Path -LiteralPath $path) -or [IO.File]::ReadAllText($path) -ne $header) { [IO.File]::WriteAllText($path, $header, [Text.UTF8Encoding]::new($false)) }
