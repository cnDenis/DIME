; DIME 迪铭五笔输入法 — Inno Setup 6 安装脚本
;
; 先运行 scripts\package.bat 组装 out\dist\DIME\（x64/x86 DLL + dict\），
; 再用本脚本编译出 out\dist\DIME-Setup.exe。也可单独用 ISCC 编译本文件。
;
; 关键点：
;   * 两个架构的 DLL 必须分别注册（同 CLSID，不同注册表视图）。
;   * 安装器以 64 位模式运行（ArchitecturesInstallIn64BitMode=x64），
;     因此 {sys} = 原生 System32（64 位 regsvr32），32 位 DLL 用
;     C:\Windows\SysWOW64\regsvr32.exe 显式注册。
;   * 目标目录使用 {autopf}\DIME = C:\Program Files\DIME。
;   * 注册需要管理员权限（PrivilegesRequired=admin）。

#define MyAppName      "DIME 迪铭五笔输入法"
; Version can be overridden from CI via "ISCC /DMyAppVersion=x.y.z".
; Falls back to MyAppVersion when built locally without the define.
#ifndef MyAppVersion
#define MyAppVersion   "1.0.1"
#endif
#define MyAppPublisher "cnDenis"
#define MyAppURL       "https://github.com/cnDenis/DIME"
#define MyDist         "..\out\dist\DIME"

[Setup]
AppId=DIME-IME
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\DIME
DefaultGroupName={#MyAppName}
OutputDir=..\out\dist
OutputBaseFilename=DIME_{#MyAppVersion}_Setup
SetupIconFile=..\image\Dime.ico
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64os
UsedUserAreasWarning=no
WizardStyle=modern
; 安装过程中若被占用，提示关闭（IME 宿主如 Word/Notepad 一般不影响文件复制）
CloseApplications=yes
; 若因文件被占用而推迟到重启后替换，安装结束时提示用户重启
RestartIfNeededByRun=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; restartreplace: if the DLL is locked (e.g. loaded by ctfmon / an IME host),
; defer the replacement to the next reboot instead of showing a "file in use"
; dialog. uninsrestartdelete: likewise defer deletion to reboot on uninstall.
Source: "{#MyDist}\dime64.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#MyDist}\dime32.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
; build_bindict.exe lives next to the DLLs; the runtime probes it there first.
Source: "{#MyDist}\build_bindict.exe"; DestDir: "{app}"; Flags: ignoreversion
; 码表文件: 替换前询问用户 (confirmoverwrite)；若文件被占用则重启后替换 (restartreplace)
Source: "{#MyDist}\dict\*"; DestDir: "{app}\dict"; Flags: ignoreversion recursesubdirs createallsubdirs confirmoverwrite restartreplace

[Run]
; 注册 64 位 DLL（原生 System32\regsvr32，写入 64 位注册表视图）
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\dime64.dll"""; StatusMsg: "正在注册 DIME（64 位）..."; Flags: runhidden
; 注册 32 位 DLL（SysWOW64\regsvr32，写入 32 位注册表视图，供 32 位程序加载）
Filename: "C:\Windows\SysWOW64\regsvr32.exe"; Parameters: "/s ""{app}\dime32.dll"""; StatusMsg: "正在注册 DIME（32 位）..."; Flags: runhidden
; 安装完成后引导用户在 Windows 语言设置中启用 DIME
Filename: "ms-settings:regionlanguage"; Description: "打开 Windows 语言设置以启用 DIME 输入法"; Flags: postinstall shellexec

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\dime64.dll"""; Flags: runhidden; RunOnceId: "unreg64"
Filename: "C:\Windows\SysWOW64\regsvr32.exe"; Parameters: "/u /s ""{app}\dime32.dll"""; Flags: runhidden; RunOnceId: "unreg32"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\dict"
Type: dirifempty; Name: "{app}"
