[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$tracked = @(& git -C $projectRoot -c core.quotepath=false ls-files)
if ($LASTEXITCODE -ne 0 -or $tracked.Count -eq 0) { throw 'Stage the intended public files before auditing.' }
$forbidden = @($tracked | Where-Object {
    $_ -match '(^|/)(tools|dist|build|temp|results|analysis|\.git)(/|$)' -or
    $_ -match '\.(key|pfx|p12|pem|log|mp4|wmv|ico|exe|dll)$' -or
    $_ -match '(^|/)(settings\.ini|imgui\.ini|AGENTS\.md|WORKFLOW\.md|workflow\.json)$'
})
if ($forbidden.Count) { throw ('Private/generated artifacts staged: ' + ($forbidden -join ', ')) }
$secretPatterns = @('gh[pousr]_[A-Za-z0-9]{30,}','github_pat_[A-Za-z0-9_]{40,}','-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----','(?i)authorization\s*[:=]\s*["'']?Bearer\s+[A-Za-z0-9_\-.]{20,}')
foreach ($file in $tracked) {
    $path = Join-Path $projectRoot $file
    if ((Get-Item -LiteralPath $path).Length -gt 20MB) { throw "Unexpected large public source file: $file" }
    $text = [IO.File]::ReadAllText($path)
    foreach ($pattern in $secretPatterns) {
        if ($text -match $pattern) { throw "Possible credential in staged file: $file" }
    }
}
Write-Output "Public source audit passed: $($tracked.Count) files; no private icons, binaries, user profiles or credential patterns."
