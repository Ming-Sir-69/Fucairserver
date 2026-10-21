[CmdletBinding()]
param([switch]$WorkspaceBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$program = & (Join-Path $PSScriptRoot 'Resolve-Application.ps1') -WorkspaceBuild:$WorkspaceBuild
$receiver = Get-Process -Name Fucairserver -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $program } | Select-Object -First 1
if (-not $receiver) { Write-Host 'Fucairserver 已退出。'; return }
if ($receiver.Path -ne $program) { throw '运行进程身份已变化，未执行退出。' }
$start = [Diagnostics.ProcessStartInfo]::new($program)
$start.ArgumentList.Add('--quit')
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WorkingDirectory = Split-Path -Parent $program
$start.Environment['PATH'] = $start.WorkingDirectory + ";$env:SystemRoot\System32;$env:SystemRoot"
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$command = [Diagnostics.Process]::Start($start)
$stdout = $command.StandardOutput.ReadToEndAsync()
$stderr = $command.StandardError.ReadToEndAsync()
if (-not $command.WaitForExit(10000)) { $command.Kill(); throw '退出命令超时。' }
if ($command.ExitCode -ne 0) { throw $stderr.Result }
if (-not $receiver.WaitForExit(15000)) { throw 'Fucairserver 尚未退出，请从托盘菜单重试。' }
Write-Host 'Fucairserver 已退出。'
