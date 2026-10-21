[CmdletBinding()]
param([string]$Proxy = $env:FUCAIRSERVER_PROXY, [switch]$UpdateLock)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $projectRoot 'profiles/runtime-source-lock.json'
$inputPath = if ($UpdateLock) { Join-Path $projectRoot 'build/runtime-source-candidates.json' } else { $lockPath }
$sources = (Get-Content -LiteralPath $inputPath -Raw | ConvertFrom-Json).sources
$destination = Join-Path $projectRoot 'build/corresponding-sources'
[IO.Directory]::CreateDirectory($destination) | Out-Null
$msysRoot = if ($env:FUCAIRSERVER_MSYS_ROOT) { $env:FUCAIRSERVER_MSYS_ROOT } else { Join-Path $projectRoot 'tools/msys64' }
$results = @($sources | ForEach-Object -ThrottleLimit 4 -Parallel {
    $ErrorActionPreference = 'Stop'
    $entry = $_
    $path = Join-Path $using:destination $entry.filename
    $proxyAddress = $using:Proxy
    $shouldUpdate = $using:UpdateLock
    function Download-Source([string]$Url, [string]$Target, [bool]$Refresh = $false) {
        if (-not $Refresh -and (Test-Path -LiteralPath $Target)) { return }
        $arguments = @('--fail','--location','--retry','3','--connect-timeout','20','--max-time','600','--silent','--show-error','--output',$Target,$Url)
        if ($proxyAddress) { $arguments = @('--proxy',$proxyAddress) + $arguments }
        & curl.exe @arguments
        if ($LASTEXITCODE -ne 0) { throw "Source download failed: $Url" }
    }
    Download-Source $entry.url $path
    if ($shouldUpdate) {
        Download-Source ($entry.url + '.sig') ($path + '.sig')
        $gpg = Join-Path $using:msysRoot 'usr/bin/gpg.exe'
        function Msys-Path([string]$WindowsPath) {
            $absolute = [IO.Path]::GetFullPath($WindowsPath).Replace('\','/')
            if ($absolute -notmatch '^[A-Za-z]:/') { throw 'Source verification requires an absolute Windows drive path.' }
            '/' + $absolute.Substring(0,1).ToLowerInvariant() + $absolute.Substring(2)
        }
        $keyring = Msys-Path (Join-Path $using:msysRoot 'etc/pacman.d/gnupg')
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            $verification = & $gpg --homedir $keyring --batch --no-auto-check-trustdb --status-fd 1 --verify (Msys-Path ($path + '.sig')) (Msys-Path $path) 2>&1 | Out-String
            if ($LASTEXITCODE -eq 0 -and $verification -match '\[GNUPG:\] VALIDSIG ') { break }
            if ($attempt -lt 2) {
                # An interrupted parallel batch can leave incomplete downloads.
                # Replace unverified cached bytes; never accept a bad signature.
                Download-Source $entry.url $path $true
                Download-Source ($entry.url + '.sig') ($path + '.sig') $true
            }
        }
        if ($LASTEXITCODE -ne 0 -or $verification -notmatch '\[GNUPG:\] VALIDSIG ') { throw "MSYS2 source signature validation failed: $($entry.filename): $verification" }
        if ($verification -notmatch '\[GNUPG:\] TRUST_(FULLY|ULTIMATE)') { throw "Source signing key is not trusted by the MSYS2 keyring: $($entry.filename)" }
    }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if (-not $shouldUpdate -and $hash -ne $entry.sha256) { throw "Corresponding-source SHA-256 mismatch: $($entry.filename)" }
    [pscustomobject]@{ base=$entry.base; version=$entry.version; filename=$entry.filename; url=$entry.url; sha256=$hash; bytes=(Get-Item -LiteralPath $path).Length }
})
if ($results.Count -ne $sources.Count) { throw 'Not all corresponding source archives passed verification.' }
if ($UpdateLock) {
    $record = @{ signature_verification='MSYS2 pacman trusted keyring'; sources=@($results | Sort-Object filename) } | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText($lockPath, $record + "`n", [Text.UTF8Encoding]::new($false))
}
[pscustomobject]@{ Verified=$results.Count; Bytes=($results | Measure-Object bytes -Sum).Sum; Directory=$destination } | ConvertTo-Json
