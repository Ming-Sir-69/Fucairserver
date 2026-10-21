$fucProjectRoot = Split-Path -Parent $PSScriptRoot
$fucMsysRoot = if ($env:FUCAIRSERVER_MSYS_ROOT) { $env:FUCAIRSERVER_MSYS_ROOT } else { Join-Path $fucProjectRoot 'tools/msys64' }
$fucToolchainBin = Join-Path $fucMsysRoot 'ucrt64/bin'
if (-not (Test-Path -LiteralPath (Join-Path $fucToolchainBin 'gcc.exe'))) { throw 'Portable UCRT64 compiler is not installed' }
$env:PATH = $fucToolchainBin + ';' + $env:PATH
$env:PKG_CONFIG_PATH = (Join-Path $fucMsysRoot 'ucrt64/lib/pkgconfig') + ';' + (Join-Path $fucMsysRoot 'ucrt64/share/pkgconfig')
$fucCompilerTemp = Join-Path $fucProjectRoot 'temp/compiler'
[IO.Directory]::CreateDirectory($fucCompilerTemp) | Out-Null
$env:TEMP = $fucCompilerTemp
$env:TMP = $fucCompilerTemp
$env:TMPDIR = $fucCompilerTemp
if ($env:FUCAIRSERVER_PROXY) {
    $env:http_proxy = $env:FUCAIRSERVER_PROXY
    $env:https_proxy = $env:FUCAIRSERVER_PROXY
}
