[CmdletBinding()]
param([switch]$WorkspaceBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $WorkspaceBuild) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
    try {
        $key = $base.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{9B108E30-D525-4A19-A905-782F635B60D5}_is1')
        if ($key) {
            try {
                $location = [string]$key.GetValue('InstallLocation')
                if ($location) {
                    $program = Join-Path $location 'Fucairserver.exe'
                    if (Test-Path -LiteralPath $program -PathType Leaf) {
                        if ((Get-Item -LiteralPath $program).VersionInfo.ProductName -eq 'Fucairserver') {
                            Write-Output ([IO.Path]::GetFullPath($program))
                            return
                        }
                    }
                }
            } finally { $key.Dispose() }
        }
    } finally { $base.Dispose() }
}
Write-Output (Join-Path $projectRoot 'dist/Fucairserver/Fucairserver.exe')
