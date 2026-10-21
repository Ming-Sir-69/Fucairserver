[CmdletBinding()]
param(
    [string]$ReceiverName,
    [ValidateRange(1,65535)][int]$ListenPort = 7010,
    [switch]$ConfigureFirewall
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$applicationDirectory = Join-Path $projectRoot 'dist/Fucairserver'
$program = Join-Path $applicationDirectory 'Fucairserver.exe'
if (-not (Test-Path -LiteralPath $program)) { throw "找不到 Fucairserver：$program" }
$hasNameOverride = $PSBoundParameters.ContainsKey('ReceiverName')
if ($hasNameOverride -and ([string]::IsNullOrWhiteSpace($ReceiverName) -or $ReceiverName.Contains('"') -or $ReceiverName.IndexOfAny([char[]]"`r`n") -ge 0)) { throw '接收端名称不能为空或包含引号、换行。' }
$runtimePath = "$applicationDirectory;$env:SystemRoot\System32;$env:SystemRoot"

$running = @(Get-CimInstance Win32_Process -Filter "Name='Fucairserver.exe'" | Where-Object { $_.ExecutablePath -eq $program })
if ($running.Count -gt 0) {
    $activation = Start-Process -FilePath $program -WorkingDirectory $applicationDirectory -WindowStyle Hidden -Environment @{PATH=$runtimePath} -PassThru
    if (-not $activation.WaitForExit(10000) -or $activation.ExitCode -ne 0) { throw '窗口恢复请求尚未完成。' }
    Write-Host 'Fucairserver 已打开。'
    return
}
if ($ConfigureFirewall) {
    $firewall = Start-Process -FilePath $program -ArgumentList '--configure-firewall' -WorkingDirectory $applicationDirectory -WindowStyle Hidden -Environment @{PATH=$runtimePath} -PassThru
    if (-not $firewall.WaitForExit(15000) -or $firewall.ExitCode -ne 0) { throw '防火墙设置未完成，请在程序内重试。' }
}
$arguments = '--listen-port ' + $ListenPort
if ($hasNameOverride) { $arguments += ' --name "' + $ReceiverName + '"' }
$displayName = if ($hasNameOverride) { $ReceiverName } else {
    $settingsFile = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Fucairserver/settings.ini'
    $savedName = if (Test-Path -LiteralPath $settingsFile) { Get-Content -LiteralPath $settingsFile -Encoding utf8 | Where-Object { $_.StartsWith('receiver_name=') } | Select-Object -Last 1 }
    if ($savedName) { $savedName.Substring('receiver_name='.Length) } else { 'Fucairserver' }
}
if (-not $hasNameOverride -and $displayName -eq 'Fucairserver 独立版') { $displayName = 'Fucairserver' }
$receiver = Start-Process -FilePath $program -ArgumentList $arguments -WorkingDirectory $applicationDirectory -WindowStyle Normal -Environment @{ PATH = $runtimePath } -PassThru
Start-Sleep -Milliseconds 800
$receiver.Refresh()
if ($receiver.HasExited) { throw "Fucairserver 启动退出，代码 $($receiver.ExitCode)。" }
Write-Host "Fucairserver 已启动：$displayName。"
