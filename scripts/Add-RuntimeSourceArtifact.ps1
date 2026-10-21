[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDir)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$lockPath = Join-Path $projectRoot 'profiles/runtime-source-lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$target = Join-Path $OutputDir "Fucairserver-$version-runtime-sources.zip"
if (Test-Path -LiteralPath $target) { throw 'Runtime source archive already exists.' }
$zip = [IO.Compression.ZipFile]::Open($target,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in $lock.sources) {
        $path = Join-Path $projectRoot ('build/corresponding-sources/' + $entry.filename)
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "Source hash mismatch: $($entry.filename)" }
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$path,$entry.filename,[IO.Compression.CompressionLevel]::NoCompression)
    }
    $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$lockPath,'runtime-source-lock.json',[IO.Compression.CompressionLevel]::Optimal)
} finally { $zip.Dispose() }
$assets = @(Get-ChildItem -LiteralPath $OutputDir -File | Where-Object Name -ne 'SHA256SUMS.txt')
$lines = @($assets | Sort-Object Name | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name })
[IO.File]::WriteAllText((Join-Path $OutputDir 'SHA256SUMS.txt'),($lines -join "`n") + "`n",[Text.UTF8Encoding]::new($false))
Write-Output $target
