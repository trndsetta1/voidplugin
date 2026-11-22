; VOIDCARVE Windows installer (Inno Setup 6)
; Compile with:  ISCC.exe installer\VoidCarve.iss   (after building the plugin)

#define AppName    "VoidCarve"
#define AppVersion "1.0.0"
#define Publisher  "VoidCarve Audio"
#define Vst3Bundle "..\build\VoidCarve_artefacts\Release\VST3\VoidCarve.vst3"

[Setup]
AppId={{6E1B7C2A-4F0D-4C55-9A8E-3D5C0A7E91B4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableProgramGroupPage=yes
UninstallFilesDir={commonpf64}\{#AppName}
UninstallDisplayName={#AppName} {#AppVersion} (VST3)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=Output
OutputBaseFilename=VoidCarve-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
InfoAfterFile=AfterInstall.txt

[Messages]
SelectDirLabel3=Setup will install the VoidCarve VST3 plugin into the folder below. The standard VST3 folder is recommended - FL Studio scans it automatically.

[Files]
Source: "{#Vst3Bundle}\*"; DestDir: "{app}\VoidCarve.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{app}\VoidCarve.vst3"
