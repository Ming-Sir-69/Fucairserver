[CmdletBinding()]
param([string[]]$InterfaceAlias)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$program = Join-Path $projectRoot 'dist/Fucairserver/Fucairserver.exe'
if (-not (Test-Path -LiteralPath $program)) { throw "未找到接收器：$program" }
if (-not $InterfaceAlias) {
    $physical = @(Get-NetAdapter -Physical | Where-Object Status -eq 'Up' | Select-Object -ExpandProperty InterfaceIndex)
    $InterfaceAlias = @(Get-NetIPConfiguration | Where-Object { $_.InterfaceIndex -in $physical -and $_.IPv4DefaultGateway } | Select-Object -ExpandProperty InterfaceAlias)
}
if (-not $InterfaceAlias) { throw '未检测到活动局域网接口，请显式传入 -InterfaceAlias。' }
$records = foreach ($protocol in 'TCP','UDP') {
    $ruleName = 'Fucairserver-Source-LAN-' + $protocol
    $existing = Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue
    if ($existing) {
        $existing | Set-NetFirewallRule -Enabled True -Direction Inbound -Action Allow -Profile Any -InterfaceAlias $InterfaceAlias | Out-Null
        $existing | Get-NetFirewallApplicationFilter | Set-NetFirewallApplicationFilter -Program $program | Out-Null
        $existing | Get-NetFirewallAddressFilter | Set-NetFirewallAddressFilter -RemoteAddress LocalSubnet | Out-Null
        $existing | Get-NetFirewallPortFilter | Set-NetFirewallPortFilter -Protocol $protocol -LocalPort Any -RemotePort Any | Out-Null
    } else {
        New-NetFirewallRule -Name $ruleName -DisplayName "Fucairserver source receiver (LAN $protocol)" -Group 'Fucairserver Source' -Program $program -Direction Inbound -Action Allow -Protocol $protocol -RemoteAddress LocalSubnet -Profile Any -InterfaceAlias $InterfaceAlias | Out-Null
    }
    [pscustomobject]@{ rule=$ruleName; program=$program; protocol=$protocol; interfaces=$InterfaceAlias; remote_address='LocalSubnet' }
}
$records | Format-Table rule,protocol,interfaces
