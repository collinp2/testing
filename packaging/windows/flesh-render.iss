; ============================================================================
;  Flesh Render — self-contained Windows installer (Inno Setup script).
;  Produces a single FleshRender-<version>-Windows.exe that drops the VST3
;  into the shared VST3 folder (C:\Program Files\Common Files\VST3) and
;  optionally installs the Standalone app + a Start-menu shortcut.
;
;  Prereqs: Inno Setup 6  (https://jrsoftware.org/isinfo.php).
;  Build the plugin first (Release):
;      cmake -B build -DCMAKE_BUILD_TYPE=Release
;      cmake --build build --config Release
;  Then, from this folder:
;      "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" flesh-render.iss
;  Output lands in ..\..\dist\.
;
;  Override the version:  ISCC.exe /DMyAppVersion=2.1.0 flesh-render.iss
; ============================================================================

#ifndef MyAppVersion
  #define MyAppVersion "2.0.0"
#endif

#define MyAppName "Flesh Render"
#define MyAppPublisher "VoidCraft Audio"
; Artefacts are emitted by JUCE here (relative to this .iss file):
#define ArtRoot "..\..\build\FleshRender_artefacts\Release"

[Setup]
AppId={{5D2E9B71-8C43-4A7F-9D06-FLESHREND201}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppPublisher}\{#MyAppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=FleshRender-{#MyAppVersion}-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#ArtRoot}\VST3\Flesh Render.vst3\*"; \
    DestDir: "{commoncf}\VST3\Flesh Render.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

Source: "{#ArtRoot}\Standalone\Flesh Render.exe"; \
    DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\Flesh Render.exe"; Components: standalone

[Run]
Filename: "{app}\Flesh Render.exe"; Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent; Components: standalone
