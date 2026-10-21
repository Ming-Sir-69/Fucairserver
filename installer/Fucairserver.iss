#ifndef ProductVersion
  #define ProductVersion "0.2.0"
#endif
#ifndef PayloadDir
  #define PayloadDir SourcePath + "..\dist\Fucairserver"
#endif
#ifndef ReleaseDir
  #define ReleaseDir SourcePath + "..\dist\releases"
#endif
#ifndef ProjectURL
  #define ProjectURL "https://github.com"
#endif

[Setup]
AppId={{9B108E30-D525-4A19-A905-782F635B60D5}
AppName=Fucairserver
AppVersion={#ProductVersion}
AppPublisher=Eric Mingle 69
AppPublisherURL={#ProjectURL}
AppSupportURL={#ProjectURL}/issues
AppUpdatesURL={#ProjectURL}/releases
VersionInfoVersion={#ProductVersion}.0
VersionInfoDescription=Fucairserver Setup
VersionInfoCopyright=Copyright (C) 2026 Fucairserver contributors
DefaultDirName={autopf}\Fucairserver
DefaultGroupName=Fucairserver
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19045
WizardStyle=modern
WizardResizable=yes
SetupLogging=no
LicenseFile=..\LICENSE
UninstallDisplayIcon={app}\Fucairserver.exe
UninstallDisplayName=Fucairserver
OutputDir={#ReleaseDir}
OutputBaseFilename=Fucairserver-{#ProductVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
CloseApplicationsFilter=Fucairserver.exe,Fucairserver.Miracast.exe,*.dll
RestartApplications=no
AllowNoIcons=yes
UsePreviousTasks=yes
DisableWelcomePage=no
ChangesAssociations=no
ChangesEnvironment=no
#if FileExists(PayloadDir + "\Fucairserver.ico")
SetupIconFile={#PayloadDir}\Fucairserver.ico
#endif
#ifdef SignToolName
SignTool={#SignToolName}
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopShortcut}"; Flags: unchecked
Name: "firewall"; Description: "{cm:AllowLocalNetwork}"; Flags: checkedonce

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Excludes: "*.log,*.key,imgui.ini,settings.ini,*.mp4,*.wmv,Launch-*.cmd"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Fucairserver"; Filename: "{app}\Fucairserver.exe"; Parameters: "--listen-port 7010"; WorkingDir: "{app}"; AppUserModelID: "Fucairserver.Receiver"
Name: "{group}\{cm:UninstallProgram,Fucairserver}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Fucairserver"; Filename: "{app}\Fucairserver.exe"; Parameters: "--listen-port 7010"; WorkingDir: "{app}"; Tasks: desktopicon; AppUserModelID: "Fucairserver.Receiver"

[Run]
Filename: "{app}\Fucairserver.exe"; Parameters: "--listen-port 7010"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,Fucairserver}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[CustomMessages]
english.DesktopShortcut=Create a desktop shortcut
chinesesimplified.DesktopShortcut=创建桌面快捷方式
english.AllowLocalNetwork=Allow devices on the local subnet to connect on private and domain networks
chinesesimplified.AllowLocalNetwork=允许专用网络和域网络中的局域网设备连接
english.CloseReceiver=Please exit Fucairserver from its tray menu, then retry. An active recording or receiving session will be interrupted.
chinesesimplified.CloseReceiver=请从托盘菜单退出 Fucairserver 后重试。正在进行的录制或接收会话将被中断。
english.NewerVersion=A newer version of Fucairserver is already installed. Please download the current release.
chinesesimplified.NewerVersion=已安装较新版本的 Fucairserver，请下载当前版本。
english.FirewallFailure=Windows could not create the local network rules. Use Allow local network in Fucairserver, or contact the administrator if a policy controls this device.
chinesesimplified.FirewallFailure=Windows 无法创建局域网规则。请在 Fucairserver 中允许局域网连接；若设备受组织策略管理，请联系管理员。

[Code]
const
  FirewallGroup = 'Fucairserver';
function ReceiverRunning: Boolean;
begin
  Result := CheckForMutexes('Local\Fucairserver.Instance.v1') or
    CheckForMutexes('Local\AirPlay-Windows.Instance.v1');
end;

function CloseReceiver: Boolean;
var
  ExitCode, Attempt: Integer;
  ProgramPath: String;
begin
  ProgramPath := ExpandConstant('{app}\Fucairserver.exe');
  if ReceiverRunning and FileExists(ProgramPath) then
    Exec(ProgramPath, '--quit', ExpandConstant('{app}'), SW_HIDE,
      ewWaitUntilTerminated, ExitCode);
  for Attempt := 1 to 100 do begin
    if not ReceiverRunning then begin Result := True; exit; end;
    Sleep(100);
  end;
  Result := not ReceiverRunning;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Existing, Incoming: Int64;
begin
  Result := '';
  if GetPackedVersion(ExpandConstant('{app}\Fucairserver.exe'), Existing) and
     StrToVersion('{#ProductVersion}.0', Incoming) and
     (ComparePackedVersion(Existing, Incoming) > 0) then begin
    Result := CustomMessage('NewerVersion');
    exit;
  end;
  if not CloseReceiver then Result := CustomMessage('CloseReceiver');
end;

procedure RemoveRule(const RuleName: String);
var
  Policy, Rule: Variant;
begin
  try
    Policy := CreateOleObject('HNetCfg.FwPolicy2');
    Rule := Policy.Rules.Item(RuleName);
    if (CompareText(Rule.ApplicationName, ExpandConstant('{app}\Fucairserver.exe')) = 0) and
       (CompareText(Rule.Grouping, FirewallGroup) = 0) then
      Policy.Rules.Remove(RuleName);
  except
    Log('Firewall rule absent or managed: ' + RuleName);
  end;
end;

procedure RemoveProductRules;
begin
  RemoveRule('Fucairserver LAN TCP');
  RemoveRule('Fucairserver LAN UDP');
end;

procedure AddRule(const RuleName: String; Protocol: Integer);
var
  Policy, Rule: Variant;
begin
  Policy := CreateOleObject('HNetCfg.FwPolicy2');
  Rule := CreateOleObject('HNetCfg.FWRule');
  Rule.Name := RuleName;
  Rule.Description := 'Fucairserver receiver: local subnet, private and domain networks.';
  Rule.ApplicationName := ExpandConstant('{app}\Fucairserver.exe');
  Rule.Grouping := FirewallGroup;
  Rule.Protocol := Protocol;
  Rule.Direction := 1;
  Rule.Action := 1;
  Rule.Profiles := 3;
  Rule.RemoteAddresses := 'LocalSubnet';
  Rule.EdgeTraversal := False;
  Rule.Enabled := True;
  Policy.Rules.Add(Rule);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('firewall') then begin
    try
      AddRule('Fucairserver LAN TCP', 6);
      AddRule('Fucairserver LAN UDP', 17);
    except
      RemoveProductRules;
      SuppressibleMsgBox(CustomMessage('FirewallFailure'), mbInformation, MB_OK, IDOK);
    end;
  end;
end;

function InitializeUninstall: Boolean;
begin
  Result := CloseReceiver;
  if not Result then SuppressibleMsgBox(CustomMessage('CloseReceiver'), mbError, MB_OK, IDOK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ExitCode: Integer;
begin
  if CurUninstallStep = usUninstall then begin
    Exec(ExpandConstant('{app}\Fucairserver.exe'), '--remove-firewall',
      ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ExitCode);
    RemoveProductRules;
  end;
end;
