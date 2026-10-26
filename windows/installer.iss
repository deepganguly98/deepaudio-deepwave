; DeepWave Windows installer (Inno Setup 6). Built by .github/workflows/windows.yml:
;   iscc /DAppVersion=0.1.0 windows\installer.iss     -> dist\DeepWave-Setup-0.1.0-x64.exe
; Expects dist\DeepWave.exe and dist\DeepWave.vst3\ (the build output).
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{B3D6A5E4-7C1E-4F0B-9E5B-3C2A7D8E1F42}
AppName=DeepWave
AppVersion={#AppVersion}
AppPublisher=DeepAudio
DefaultDirName={autopf}\DeepAudio\DeepWave
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=DeepWave-Setup-{#AppVersion}-x64
SetupIconFile=..\assets\DeepWave.ico
UninstallDisplayIcon={app}\DeepWave.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; the VST3 goes to C:\Program Files\Common Files\VST3, which needs administrator rights
PrivilegesRequired=admin
InfoAfterFile=after_install.txt

[Types]
Name: "full"; Description: "Plugin (VST3) and standalone app"
Name: "plugin"; Description: "Plugin (VST3) only, for FL Studio and other hosts"
Name: "app"; Description: "Standalone app only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (FL Studio, Ableton Live, Reaper, Cubase, Bitwig, Studio One)"; Types: full plugin custom
Name: "app"; Description: "Standalone app"; Types: full app custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: app; Flags: unchecked

[Files]
Source: "..\dist\DeepWave.vst3\*"; DestDir: "{commoncf64}\VST3\DeepWave.vst3"; Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\dist\DeepWave.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\DeepWave"; Filename: "{app}\DeepWave.exe"; Components: app
Name: "{autodesktop}\DeepWave"; Filename: "{app}\DeepWave.exe"; Tasks: desktopicon; Components: app

[Run]
Filename: "{app}\DeepWave.exe"; Description: "Start DeepWave now"; Flags: nowait postinstall skipifsilent; Components: app
