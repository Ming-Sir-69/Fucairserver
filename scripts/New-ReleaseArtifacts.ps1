[CmdletBinding()]
param([Parameter(Mandatory)][string]$PayloadDir, [string]$OutputDir, [switch]$SkipRuntimeSources)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
$PayloadDir = [IO.Path]::GetFullPath($PayloadDir)
if (-not $OutputDir) { $OutputDir = Join-Path $projectRoot ('dist/releases/v' + $version) }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$stage = Join-Path $projectRoot ('build/source-package-' + [guid]::NewGuid().ToString('N'))
$sourceRoot = Join-Path $stage 'Fucairserver'
[IO.Directory]::CreateDirectory($sourceRoot) | Out-Null
[IO.Directory]::CreateDirectory($OutputDir) | Out-Null
$tar = Join-Path $stage 'product.tar'
& git -C $projectRoot archive --format=tar ('--output=' + $tar) HEAD
if ($LASTEXITCODE -ne 0) { throw 'Product git archive failed; commit the release source first.' }
& tar.exe -xf $tar -C $sourceRoot
if ($LASTEXITCODE -ne 0) { throw 'Product source extraction failed.' }
$trees = foreach ($entry in (Get-Content -LiteralPath (Join-Path $projectRoot 'profiles/source-lock.json') -Raw | ConvertFrom-Json).sources) {
    $upstream = Join-Path $projectRoot $entry.directory
    $actualRevision = (& git -C $upstream rev-parse HEAD).Trim()
    if ($actualRevision -ne $entry.commit) { throw "Wrong upstream revision: $($entry.name)" }
    $files = foreach ($file in @(& git -C $upstream -c core.quotepath=false ls-files)) {
        $original = Join-Path $upstream $file
        if (-not (Test-Path -LiteralPath $original -PathType Leaf)) { throw "Missing upstream source: $($entry.name)/$file" }
        $target = Join-Path (Join-Path $sourceRoot $entry.directory) $file
        [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
        Copy-Item -LiteralPath $original -Destination $target
        [pscustomobject]@{ path=$file; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    [pscustomobject]@{ directory=$entry.directory; commit=$entry.commit; patched=($entry.name -eq 'AirPlay-Windows'); files=@($files) }
}
$bundle = [pscustomobject]@{ version=$version; trees=@($trees) } | ConvertTo-Json -Depth 7
[IO.File]::WriteAllText((Join-Path $sourceRoot 'source-bundle.json'),$bundle + "`n",[Text.UTF8Encoding]::new($false))
if (Test-Path -LiteralPath (Join-Path $sourceRoot 'assets/Fucairserver.ico')) { throw 'Private icon entered the source package.' }
$sourceZip = Join-Path $OutputDir ("Fucairserver-$version-source.zip")
if (Test-Path -LiteralPath $sourceZip) { throw 'Source archive already exists; use a fresh release output directory.' }
[IO.Compression.ZipFile]::CreateFromDirectory($sourceRoot,$sourceZip,[IO.Compression.CompressionLevel]::Optimal,$false)
$portableZip = Join-Path $OutputDir ("Fucairserver-$version-windows-x64.zip")
if (Test-Path -LiteralPath $portableZip) { throw 'Portable archive already exists; use a fresh release output directory.' }
[IO.Compression.ZipFile]::CreateFromDirectory($PayloadDir,$portableZip,[IO.Compression.CompressionLevel]::Optimal,$false)
if (-not $SkipRuntimeSources) {
$runtimeSource = Join-Path $projectRoot 'build/corresponding-sources'
$sourceLockPath = Join-Path $projectRoot 'profiles/runtime-source-lock.json'
$sourceLock = Get-Content -LiteralPath $sourceLockPath -Raw | ConvertFrom-Json
$runtimeZip = Join-Path $OutputDir ("Fucairserver-$version-runtime-sources.zip")
if (Test-Path -LiteralPath $runtimeZip) { throw 'Runtime source archive already exists; use a fresh release output directory.' }
$zip = [IO.Compression.ZipFile]::Open($runtimeZip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in $sourceLock.sources) {
        $path = Join-Path $runtimeSource $entry.filename
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) { throw "Corresponding source failed hash validation: $($entry.filename)" }
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$path,$entry.filename,[IO.Compression.CompressionLevel]::NoCompression)
    }
    $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$sourceLockPath,'runtime-source-lock.json',[IO.Compression.CompressionLevel]::Optimal)
} finally { $zip.Dispose() }
}
Copy-Item -LiteralPath (Join-Path $PayloadDir 'sbom.cdx.json') -Destination (Join-Path $OutputDir ("Fucairserver-$version-sbom.cdx.json"))
$assets = @(Get-ChildItem -LiteralPath $OutputDir -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' })
$checksums = @($assets | Sort-Object Name | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name })
[IO.File]::WriteAllText((Join-Path $OutputDir 'SHA256SUMS.txt'),($checksums -join "`n") + "`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]@{ Version=$version; Directory=$OutputDir; Assets=@($assets | Select-Object Name,Length) } | ConvertTo-Json -Depth 4
