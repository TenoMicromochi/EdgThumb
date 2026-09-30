; EdgThumb installer - Inno Setup 6
; Build:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\EdgThumb.iss
; Needs build\EdgThumb.dll (run build.bat first). Output: dist\EdgThumbSetup-<version>.exe

#define MyAppName "EdgThumb"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Micromochi_Teno"
#define MyAppId "MicromochiTeno.EdgThumb"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf64}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
ShowLanguageDialog=yes
InfoAfterFile=README.txt
OutputDir=..\dist
OutputBaseFilename=EdgThumbSetup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
; Tells the shell that file associations changed (SHChangeNotify with
; SHCNE_ASSOCCHANGED) when the install or uninstall finishes.
ChangesAssociations=yes
VersionInfoVersion={#MyAppVersion}
UninstallDisplayIcon={app}\EdgThumb.dll

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
en.RestartExplorerPrompt=Explorer needs to restart before the thumbnails show up.%nRestart Explorer now? (Open windows and the taskbar disappear for a moment.)
ja.RestartExplorerPrompt=サムネイルを表示するには Explorer の再起動が必要です。%n今すぐ Explorer を再起動しますか？（開いているウィンドウとタスクバーが一瞬消えます）
en.ExplorerRestartFailed=Failed to restart Explorer.
ja.ExplorerRestartFailed=Explorer の再起動に失敗しました。

[Files]
Source: "..\build\EdgThumb.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; A brace that opens a GUID is written "{{" because Inno Setup reads "{" as the
; start of a constant.
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{34907B7E-9EC3-4494-8D6B-D7EBD3E19F64}"; ValueType: string; ValueName: ""; ValueData: "EDGE2 (.edg) Thumbnail Provider"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{34907B7E-9EC3-4494-8D6B-D7EBD3E19F64}\InprocServer32"; ValueType: string; ValueName: ""; ValueData: "{app}\EdgThumb.dll"
Root: HKLM64; Subkey: "Software\Classes\CLSID\{{34907B7E-9EC3-4494-8D6B-D7EBD3E19F64}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"
; The default value of .edg (EDGE2's own document type) is left alone.
Root: HKLM64; Subkey: "Software\Classes\.edg\ShellEx\{{e357fccd-a995-4576-b01f-234630154e96}"; ValueType: string; ValueName: ""; ValueData: "{{34907B7E-9EC3-4494-8D6B-D7EBD3E19F64}"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Classes\.edg"; ValueType: string; ValueName: "PerceivedType"; ValueData: "image"; Flags: uninsdeletevalue

[Code]
procedure RestartExplorerIfWanted;
var
  ResultCode: Integer;
begin
  if SuppressibleMsgBox(ExpandConstant('{cm:RestartExplorerPrompt}'),
                        mbConfirmation, MB_YESNO, IDNO) <> IDYES then
    Exit;
  if not Exec(ExpandConstant('{sys}\cmd.exe'),
              '/C taskkill /F /IM explorer.exe >nul 2>&1 & start "" explorer.exe',
              '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Log(ExpandConstant('{cm:ExplorerRestartFailed}'));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssDone then
    RestartExplorerIfWanted;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usDone then
    RestartExplorerIfWanted;
end;
