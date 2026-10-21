[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]]$Path,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{40}$')][string]$CertificateThumbprint,
    [Parameter(Mandatory)][string]$SignTool,
    [uri]$TimestampServer = 'https://timestamp.digicert.com'
)
$ErrorActionPreference = 'Stop'
$certificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $CertificateThumbprint)
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date)) { throw 'A valid code-signing identity with a private key is required.' }
foreach ($file in $Path) {
    $absolute = (Resolve-Path -LiteralPath $file).Path
    & $SignTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampServer.AbsoluteUri /td SHA256 $absolute
    if ($LASTEXITCODE -ne 0) { throw "Signing failed: $file" }
    & $SignTool verify /pa /all $absolute
    if ($LASTEXITCODE -ne 0 -or (Get-AuthenticodeSignature -LiteralPath $absolute).Status -ne 'Valid') { throw "Publisher signature verification failed: $file" }
}
