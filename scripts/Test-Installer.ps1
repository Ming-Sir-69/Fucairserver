# End-to-end installer lifecycle. Run only on a test machine or with a fresh
# isolated -InstallDir under this workspace's build directory.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Installer,
    [string]$InstallDir,
    [ValidateRange(1024,65535)][int]$Port = 17010
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$Installer = [IO.Path]::GetFullPath($Installer)
if (-not $InstallDir) { $InstallDir = Join-Path $projectRoot ('build/acceptance/run-' + [guid]::NewGuid().ToString('N') + '/安装 App') }
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/acceptance')) + [IO.Path]::DirectorySeparatorChar
if (-not $InstallDir.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Acceptance installations must be inside this workspace build/acceptance/ directory.' }
if (Test-Path -LiteralPath $InstallDir) { throw 'Acceptance installation directory must not already exist.' }
$appId = '{9B108E30-D525-4A19-A905-782F635B60D5}_is1'
$registryPath = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\' + $appId
if (Test-Path -LiteralPath $registryPath) { throw 'An installed product already owns this AppId. Run acceptance on a clean machine; the script will not replace it.' }
if (Get-Process -Name Fucairserver -ErrorAction SilentlyContinue) { throw 'Exit Fucairserver before installer acceptance.' }
$dataDirectory = Join-Path (Split-Path -Parent $InstallDir) '配置 data'
[IO.Directory]::CreateDirectory($dataDirectory) | Out-Null
$checks = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$Label) {
    if (-not $Condition) { throw "Acceptance failed: $Label" }
    $checks.Add($Label)
    Write-Host "PASS $Label"
}
function Run-Setup {
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/LANG=english',('/DIR="' + $InstallDir + '"'),'/TASKS=firewall')
    $setup = Start-Process -FilePath $Installer -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $setup.WaitForExit(90000)) { throw 'Installer timed out.' }
    Check ($setup.ExitCode -eq 0) 'Installer exits successfully without reboot'
}
function Run-App([string[]]$Arguments, [bool]$Capture = $false) {
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $InstallDir 'Fucairserver.exe'))
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WorkingDirectory = $InstallDir
    $info.Environment['PATH'] = $InstallDir + ';' + $env:SystemRoot + '\System32;' + $env:SystemRoot
    $info.Environment['FUCAIRSERVER_DATA_DIR'] = $dataDirectory
    $info.RedirectStandardOutput = $Capture
    $info.RedirectStandardError = $Capture
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    [Diagnostics.Process]::Start($info)
}
function Rtsp-Ready([Diagnostics.Process]$Process) {
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        $Process.Refresh()
        if ($Process.HasExited) { throw "Installed receiver exited: $($Process.ExitCode)" }
        $tcp = [Net.Sockets.TcpClient]::new()
        try {
            $pending = $tcp.ConnectAsync('127.0.0.1', $Port)
            if ($pending.Wait(150) -and $tcp.Connected) {
                $stream = $tcp.GetStream(); $stream.ReadTimeout = 2000
                $bytes = [Text.Encoding]::ASCII.GetBytes("OPTIONS * RTSP/1.0`r`nCSeq: 1`r`n`r`n")
                $stream.Write($bytes,0,$bytes.Length)
                $reply = [byte[]]::new(4096)
                $length = $stream.Read($reply,0,$reply.Length)
                if ([Text.Encoding]::ASCII.GetString($reply,0,$length) -match '^RTSP/1.0 200') { return $true }
            }
        } catch { } finally { $tcp.Dispose() }
        Start-Sleep -Milliseconds 100
    }
    return $false
}
function Exit-App([Diagnostics.Process]$Process) {
    $quit = Run-App @('--quit') $true
    Check ($quit.WaitForExit(10000) -and $quit.ExitCode -eq 0) 'Tray receiver accepts graceful quit'
    Check ($Process.WaitForExit(15000)) 'Receiver releases sockets and exits'
}
$receiver = $null
$installed = $false
try {
    Run-Setup
    $installed = $true
    $program = Join-Path $InstallDir 'Fucairserver.exe'
    Check (Test-Path -LiteralPath $program) 'Installed executable exists in Unicode path with spaces'
    $registered = Get-ItemProperty -LiteralPath $registryPath
    Check ($registered.DisplayName -eq 'Fucairserver' -and $registered.InstallLocation.TrimEnd('\') -eq $InstallDir.TrimEnd('\')) 'Windows Apps uninstall entry points to this installation'
    $manifest = Get-Content -LiteralPath (Join-Path $InstallDir 'release-manifest.json') -Raw | ConvertFrom-Json
    Check ($manifest.version -eq $registered.DisplayVersion) 'Executable package and installer versions agree'
    Check (-not (Test-Path -LiteralPath (Join-Path $InstallDir 'Fucairserver.ico'))) 'Public installer excludes private icon'
    foreach ($protocol in 'TCP','UDP') {
        $rule = Get-NetFirewallRule -DisplayName ('Fucairserver LAN ' + $protocol)
        $filter = $rule | Get-NetFirewallApplicationFilter
        $address = $rule | Get-NetFirewallAddressFilter
        Check ($filter.Program -eq $program -and $address.RemoteAddress -contains 'LocalSubnet' -and $rule.Profile.ToString() -notmatch 'Public') ("$protocol firewall rule is application-specific and local-subnet-only")
    }
    $shortcut = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Fucairserver/Fucairserver.lnk'
    Check (Test-Path -LiteralPath $shortcut) 'Start menu shortcut exists'
    $receiver = Run-App @('--listen-port', $Port.ToString(), '--name', '验收接收器')
    Check (Rtsp-Ready $receiver) 'Installed receiver responds RTSP 200 with sanitized PATH'
    $owner = Get-NetTCPConnection -State Listen -LocalPort $Port
    Check (@($owner | Where-Object OwningProcess -eq $receiver.Id).Count -gt 0) 'Installed process owns the tested listener'
    $secondary = Run-App @('--listen-port', $Port.ToString()) $true
    Check ($secondary.WaitForExit(10000) -and $secondary.ExitCode -eq 0) 'Second launch activates the existing instance'
    Exit-App $receiver
    $settings = Join-Path $dataDirectory 'settings.ini'
    $key = Join-Path $dataDirectory 'receiver.key'
    Check ((Get-Content -LiteralPath $settings -Raw) -match 'receiver_name=验收接收器') 'Unicode receiver name persists in user profile'
    if (-not (Test-Path -LiteralPath $key)) {
        $keys = @(Get-ChildItem -LiteralPath $dataDirectory -File | Where-Object Extension -eq '.key')
        Check ($keys.Count -eq 1) 'Pairing key is stored outside program directory'
        $key = $keys[0].FullName
    }
    $keyHash = (Get-FileHash -LiteralPath $key -Algorithm SHA256).Hash
    $settingsHash = (Get-FileHash -LiteralPath $settings -Algorithm SHA256).Hash
    Check (-not (Test-Path -LiteralPath (Join-Path $InstallDir 'imgui.ini'))) 'Application creates no layout file in installed program directory'
    # Repair/upgrade while the application is running must close it gracefully.
    $receiver = Run-App @('--listen-port', $Port.ToString())
    Check (Rtsp-Ready $receiver) 'Receiver restarts from persisted settings'
    Run-Setup
    Check ($receiver.WaitForExit(15000)) 'Running app exits before upgrade replaces its files'
    Check ((Get-FileHash -LiteralPath $key -Algorithm SHA256).Hash -eq $keyHash) 'Repair/upgrade preserves pairing identity'
    Check ((Get-FileHash -LiteralPath $settings -Algorithm SHA256).Hash -eq $settingsHash) 'Repair/upgrade preserves saved settings'
    $receiver = Run-App @('--listen-port', $Port.ToString())
    Check (Rtsp-Ready $receiver) 'Upgraded app starts and accepts RTSP'
    $uninstall = Start-Process -FilePath (Join-Path $InstallDir 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -PassThru
    Check ($uninstall.WaitForExit(90000) -and $uninstall.ExitCode -eq 0) 'Uninstall exits successfully without reboot'
    $installed = $false
    Check ($receiver.WaitForExit(15000)) 'Uninstaller gracefully closes the running receiver'
    Check (-not (Test-Path -LiteralPath $program)) 'Uninstaller removes installed product executable'
    Check (-not (Test-Path -LiteralPath $registryPath)) 'Uninstaller removes Windows Apps registration'
    Check (-not (Test-Path -LiteralPath $shortcut)) 'Uninstaller removes Start menu shortcut'
    Check (@(Get-NetFirewallRule -DisplayName 'Fucairserver LAN TCP','Fucairserver LAN UDP' -ErrorAction SilentlyContinue).Count -eq 0) 'Uninstaller removes its network rules'
    Check ((Get-FileHash -LiteralPath $key -Algorithm SHA256).Hash -eq $keyHash -and (Test-Path -LiteralPath $settings)) 'Uninstaller preserves user data and pairing identity'
    $reportPath = Join-Path (Split-Path -Parent $InstallDir) 'acceptance.json'
    [pscustomobject]@{ Passed=$checks.Count; OS=[Environment]::OSVersion.Version.ToString(); Version=$manifest.version; Checks=@($checks) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $reportPath -Encoding utf8
    Write-Output "Installer acceptance passed: $($checks.Count) checks."
} finally {
    if ($receiver -and -not $receiver.HasExited) {
        try { $quit = Run-App @('--quit') $true; $null = $quit.WaitForExit(10000); $null = $receiver.WaitForExit(15000) } catch { Write-Warning 'Receiver needs a manual tray exit.' }
    }
    if ($installed -and (Test-Path -LiteralPath (Join-Path $InstallDir 'unins000.exe'))) {
        $cleanup = Start-Process -FilePath (Join-Path $InstallDir 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -PassThru
        $null = $cleanup.WaitForExit(90000)
    }
}
