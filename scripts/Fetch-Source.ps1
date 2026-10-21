[CmdletBinding()]
param([string]$Proxy = $env:FUCAIRSERVER_PROXY)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$gitNetworkOptions = @()
if ($Proxy) { $gitNetworkOptions = @('-c', ('http.proxy=' + $Proxy)) }
$sources = Get-Content -LiteralPath (Join-Path $projectRoot 'profiles/source-lock.json') -Raw | ConvertFrom-Json
$bundlePath = Join-Path $projectRoot 'source-bundle.json'
if (Test-Path -LiteralPath $bundlePath) {
    $bundle = Get-Content -LiteralPath $bundlePath -Raw | ConvertFrom-Json
    foreach ($entry in $sources.sources) {
        $tree = @($bundle.trees | Where-Object { $_.directory -eq $entry.directory -and $_.commit -eq $entry.commit })
        if ($tree.Count -ne 1) { throw "Source bundle revision mismatch: $($entry.name)" }
        foreach ($file in $tree[0].files) {
            $treeRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $entry.directory)) + [IO.Path]::DirectorySeparatorChar
            $path = [IO.Path]::GetFullPath((Join-Path $treeRoot $file.path))
            if (-not $path.StartsWith($treeRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Source bundle contains an invalid path.' }
            if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) { throw "Source bundle file differs: $($entry.name)/$($file.path)" }
        }
    }
    Write-Output 'Verified the bundled, patched source trees.'
    return
}
foreach ($entry in $sources.sources) {
    $destination = Join-Path $projectRoot $entry.directory
    if (-not (Test-Path -LiteralPath (Join-Path $destination '.git'))) {
        & git @gitNetworkOptions -c core.longpaths=true clone --filter=blob:none $entry.url $destination
        if ($LASTEXITCODE -ne 0) { throw "Clone failed: $($entry.name)" }
        & git -C $destination checkout --detach $entry.commit
        if ($LASTEXITCODE -ne 0) { throw "Source checkout failed: $($entry.name)" }
    }
    $head = (& git -C $destination rev-parse HEAD).Trim()
    if ($head -ne $entry.commit) { throw "Source HEAD differs from lock: $($entry.name)" }
}
$patch = Join-Path $projectRoot 'scripts/patches/native-receiver.patch'
if (-not (Test-Path -LiteralPath $patch)) { throw 'Fucairserver 源码补丁缺失，不能以未定制上游代替本工程。' }
if (Test-Path -LiteralPath $patch) {
    $receiver = Join-Path $projectRoot 'third_party/AirPlay-Windows'
    & git -C $receiver apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) {
        & git -C $receiver apply --check $patch
        if ($LASTEXITCODE -ne 0) { throw 'Source patch cannot be applied to the selected revision' }
        & git -C $receiver apply $patch
        if ($LASTEXITCODE -ne 0) { throw 'Source patch application failed' }
    }
}
