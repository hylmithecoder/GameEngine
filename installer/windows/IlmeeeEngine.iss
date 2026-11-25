; Ilmeee Engine — Windows installer (Inno Setup 6).
; Built by installer/windows/package.sh, which passes:
;   /DAppVersion=<x.y.z>  /DStageDir=<staged folder>  /DOutputDir=<dist>  /DOutputName=<file>
; Shortcuts are only created for the programs that exist in the staged folder,
; so the same script works at every stage of the Windows port.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef StageDir
  #define StageDir "..\..\dist\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
#ifndef OutputName
  #define OutputName "IlmeeeEngine-setup"
#endif

#define AppName "Ilmeee Engine"
#define HasHub FileExists(StageDir + "\IlmeeeHub.exe")
#define HasEditor FileExists(StageDir + "\GameEngineSDL.exe")

[Setup]
; Keep this AppId forever: upgrades and uninstall find the installation by it.
AppId={{5B8E5E0C-3D1F-4C55-9E7A-1F1E1E3E3E01}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Ilmeee
AppPublisherURL=https://github.com/hylmithecoder/GameEngine
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile={#StageDir}\IlmeeeEngine.ico
UninstallDisplayIcon={app}\IlmeeeEngine.ico
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install is allowed (no admin rights needed); the dialog lets the
; user pick "for all users" instead.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
#if HasHub
Name: "{group}\{#AppName} Hub"; Filename: "{app}\IlmeeeHub.exe"; WorkingDir: "{app}"; IconFilename: "{app}\IlmeeeEngine.ico"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\IlmeeeHub.exe"; WorkingDir: "{app}"; IconFilename: "{app}\IlmeeeEngine.ico"; Tasks: desktopicon
#elif HasEditor
Name: "{group}\{#AppName} Editor"; Filename: "{app}\GameEngineSDL.exe"; WorkingDir: "{app}"; IconFilename: "{app}\IlmeeeEngine.ico"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\GameEngineSDL.exe"; WorkingDir: "{app}"; IconFilename: "{app}\IlmeeeEngine.ico"; Tasks: desktopicon
#endif
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[Run]
#if HasHub
Filename: "{app}\IlmeeeHub.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser
#elif HasEditor
Filename: "{app}\GameEngineSDL.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser
#endif

[Code]
// The engine renders with Vulkan; the loader (vulkan-1.dll) comes with the
// GPU driver. Warn, but do not block: the driver may be installed later.
function InitializeSetup(): Boolean;
begin
  Result := True;
  if not FileExists(ExpandConstant('{sys}\vulkan-1.dll')) then
    MsgBox('Vulkan was not found on this PC (vulkan-1.dll).' + #13#10 +
           'Ilmeee Engine needs a GPU driver with Vulkan support. ' +
           'Install or update your graphics driver before running it.',
           mbInformation, MB_OK);
end;
