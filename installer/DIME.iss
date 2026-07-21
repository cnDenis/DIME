; DIME 迪弥五笔输入法 — Inno Setup 6 安装脚本
;
; 先运行 scripts\package.bat 组装 out\dist\DIME\（x64/x86 DLL + dict\），
; 再用本脚本编译出 out\dist\DIME-Setup.exe。也可单独用 ISCC 编译本文件。
;
; 要点：
;   * 两个架构的 DLL 必须分别注册（同 CLSID，不同注册表视图）。
;   * 安装器以 64 位模式运行（ArchitecturesInstallIn64BitMode=x64），
;     因此 {sys} = 原生 System32（64 位 regsvr32），32 位 DLL 用
;     C:\Windows\SysWOW64\regsvr32.exe 显式注册。
;   * 目标目录使用 {autopf}\DIME = C:\Program Files\DIME。
;   * 注册需要管理员权限（PrivilegesRequired=admin）。
;   * 通过卸载项 AppId 判断「全新安装」还是「升级」，向导文案随之切换。
;   * 再次运行安装包且已安装时, 可选择升级或卸载。

#define MyAppId        "DIME-IME"
#define MyAppName      "DIME 迪弥五笔输入法"
; Version is supplied by package.bat / CI as ISCC /DMyAppVersion=...
; (v* tag if present, else Version.h + date). Fallback for a bare ISCC run.
#ifndef MyAppVersion
#define MyAppVersion   "1.0.0"
#endif
; 文件版本必须是 a.b.c[.d] 纯数字. AppVersion 常带 -yyyyMMdd 后缀, 直接套会变成 0.0.0.0.
#if Pos("-", MyAppVersion) > 0
  #define MyFileVersion Copy(MyAppVersion, 1, Pos("-", MyAppVersion) - 1)
#else
  #define MyFileVersion MyAppVersion
#endif
#define MyAppPublisher "cnDenis"
#define MyAppURL       "https://github.com/cnDenis/DIME"
#define MyDist         "..\out\dist\DIME"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\DIME
; 始终显示「选择目标位置」, 允许用户改安装目录 (默认仍为 Program Files\DIME)
DisableDirPage=no
; 无开始菜单快捷方式, 不创建程序组
DisableProgramGroupPage=yes
OutputDir=..\out\dist
OutputBaseFilename=DIME_{#MyAppVersion}_Setup
SetupIconFile=..\image\Dime.ico
; 右键属性 → 详细信息
VersionInfoVersion={#MyFileVersion}
VersionInfoProductTextVersion={#MyAppVersion}
VersionInfoCopyright=Copyright (C) 2026 {#MyAppPublisher}
VersionInfoDescription={#MyAppName} 安装程序
VersionInfoProductName={#MyAppName}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64os
UsedUserAreasWarning=no
WizardStyle=modern
; 不提示关闭正在运行的程序; DLL 用 restartreplace, 占用时改到重启后替换.
CloseApplications=no
; 若因文件被占用而推迟到重启后替换，安装结束时提示用户重启
RestartIfNeededByRun=yes

[Languages]
; 简体中文向导 (isl 随仓库分发, 不依赖本机 Inno Languages 目录)
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"

[CustomMessages]
; 升级场景覆盖默认「安装」文案 (由 [Code] 在启动时套用)
; 注意: CustomMessages 不会展开 [name]/[name/ver], 须用 {#...} 编译期写入.
UpgradeWelcomeLabel1=欢迎使用 {#MyAppName} 升级向导
UpgradeWelcomeLabel2=检测到本机已安装 {#MyAppName}。%n%n即将升级到 {#MyAppName} {#MyAppVersion}。下一页可选择是否保留现有码表。
UpgradeReadyMemo=检测到已安装版本, 将执行升级。%n%n目标位置:%n  {app}%n%n版本:%n  {#MyAppVersion}
UpgradeFinishedLabel=已成功将 {#MyAppName} 升级到本机。
UpgradeButtonInstall=升级(&U)
UpgradeSetupWindowTitle=升级 - {#MyAppName} {#MyAppVersion}
TaskPreserveDict=保留现有码表 (不覆盖 dict 目录中的文件)
ExistingInstallInstruction=本机已安装 {#MyAppName}
ExistingInstallText=请选择要进行的操作。
ExistingInstallUpgradeBtn=升级到 {#MyAppVersion}%n安装或更新到原目录, 下一页可选择是否保留码表
ExistingInstallUninstallBtn=卸载%n移除已安装的版本
UninstallNotFound=未找到卸载程序, 请到「设置 → 应用」中卸载 {#MyAppName}。

[Tasks]
; 仅升级时出现; 默认勾选, 避免覆盖用户改过的码表
Name: preserveDict; Description: "{cm:TaskPreserveDict}"; Flags: checkedonce; Check: IsUpgrade

[Files]
; restartreplace: if the DLL is locked (e.g. loaded by ctfmon / an IME host),
; defer the replacement to the next reboot instead of showing a "file in use"
; dialog. uninsrestartdelete: likewise defer deletion to reboot on uninstall.
Source: "{#MyDist}\dime64.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#MyDist}\dime32.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
; build_bindict.exe lives next to the DLLs; the runtime probes it there first.
Source: "{#MyDist}\build_bindict.exe"; DestDir: "{app}"; Flags: ignoreversion
; Settings entry: open the same panel as language-settings Options / status-bar menu.
Source: "{#MyDist}\dime_config.exe"; DestDir: "{app}"; Flags: ignoreversion
; 码表: 全新安装必写; 升级时若勾选「保留现有码表」则跳过 (见 ShouldInstallDict)
Source: "{#MyDist}\dict\*"; DestDir: "{app}\dict"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace; Check: ShouldInstallDict

[Run]
; 注册 64 位 DLL（原生 System32\regsvr32，写入 64 位注册表视图）
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\dime64.dll"""; StatusMsg: "正在注册 DIME（64 位）..."; Flags: runhidden
; 注册 32 位 DLL（SysWOW64\regsvr32，写入 32 位注册表视图，供 32 位程序加载）
Filename: "C:\Windows\SysWOW64\regsvr32.exe"; Parameters: "/s ""{app}\dime32.dll"""; StatusMsg: "正在注册 DIME（32 位）..."; Flags: runhidden
; 全新安装: 引导启用输入法; 升级: 默认不勾选 (一般已启用)
Filename: "ms-settings:regionlanguage"; Description: "打开 Windows 语言设置以启用 DIME 输入法"; Flags: postinstall shellexec; Check: not IsUpgrade
Filename: "ms-settings:regionlanguage"; Description: "打开 Windows 语言设置"; Flags: postinstall shellexec unchecked; Check: IsUpgrade

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\dime64.dll"""; Flags: runhidden; RunOnceId: "unreg64"
Filename: "C:\Windows\SysWOW64\regsvr32.exe"; Parameters: "/u /s ""{app}\dime32.dll"""; Flags: runhidden; RunOnceId: "unreg32"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\dict"
Type: dirifempty; Name: "{app}"

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1';

function IsUpgrade: Boolean;
begin
  { 本机已有同 AppId 的卸载项, 即视为升级 (含改目录重装). }
  Result := RegKeyExists(HKLM, UninstallKey) or RegKeyExists(HKCU, UninstallKey);
end;

function GetUninstallString: String;
begin
  Result := '';
  if not RegQueryStringValue(HKLM, UninstallKey, 'UninstallString', Result) then
    RegQueryStringValue(HKCU, UninstallKey, 'UninstallString', Result);
end;

function InitializeSetup: Boolean;
var
  Uninst: String;
  ResultCode: Integer;
  Choice: Integer;
  Buttons: TArrayOfString;
begin
  Result := True;
  if not IsUpgrade then
    Exit;

  { TaskDialog: 按钮文案直接写「升级/卸载」, 比是/否/取消清晰.
    注意: 数组字面量的 [ 不能顶格, 否则会被当成节标签. }
  SetArrayLength(Buttons, 2);
  Buttons[0] := ExpandConstant('{cm:ExistingInstallUpgradeBtn}');
  Buttons[1] := ExpandConstant('{cm:ExistingInstallUninstallBtn}');
  Choice := TaskDialogMsgBox(
    ExpandConstant('{cm:ExistingInstallInstruction}'),
    ExpandConstant('{cm:ExistingInstallText}'),
    mbConfirmation, MB_YESNOCANCEL, Buttons, IDYES);

  case Choice of
    IDYES:
      Result := True;
    IDNO:
      begin
        Uninst := RemoveQuotes(GetUninstallString);
        if Uninst <> '' then
          Exec(Uninst, '', '', SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode)
        else
          MsgBox(ExpandConstant('{cm:UninstallNotFound}'), mbError, MB_OK);
        Result := False;
      end;
    else
      Result := False;
  end;
end;

function ShouldInstallDict: Boolean;
begin
  { 全新安装: 任务不出现, WizardIsTaskSelected 为 False → 写入码表.
    升级且勾选保留: 跳过 dict\; 取消勾选: 覆盖为安装包内码表. }
  Result := not WizardIsTaskSelected('preserveDict');
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  if IsUpgrade then
  begin
    { cm 文案内的 app 常量需再 ExpandConstant 一次. }
    Result := ExpandConstant(ExpandConstant('{cm:UpgradeReadyMemo}'));
    if MemoTasksInfo <> '' then
      Result := Result + NewLine + NewLine + MemoTasksInfo;
  end
  else
    Result := MemoDirInfo + NewLine + NewLine + MemoGroupInfo;
end;

procedure ApplyUpgradeMessages;
begin
  if not IsUpgrade then
    Exit;

  WizardForm.WelcomeLabel1.Caption := ExpandConstant('{cm:UpgradeWelcomeLabel1}');
  WizardForm.WelcomeLabel2.Caption := ExpandConstant('{cm:UpgradeWelcomeLabel2}');
  WizardForm.FinishedLabel.Caption := ExpandConstant('{cm:UpgradeFinishedLabel}');
  WizardForm.Caption := ExpandConstant('{cm:UpgradeSetupWindowTitle}');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if not IsUpgrade then
    Exit;

  { 仅在「准备安装」页把「安装」换成「升级」; 其他页保持「下一步」. }
  if CurPageID = wpReady then
    WizardForm.NextButton.Caption := ExpandConstant('{cm:UpgradeButtonInstall}');
end;

procedure InitializeWizard;
begin
  ApplyUpgradeMessages;
end;
