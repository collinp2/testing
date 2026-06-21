; ============================================================================
;  NECRONAM MAX — self-contained Windows installer (Inno Setup script).
;  Produces a single NECRONAM-MAX-<version>-Windows.exe that drops the VST3
;  into the shared VST3 folder (C:\Program Files\Common Files\VST3) and
;  optionally installs the Standalone app + a Start-menu shortcut.
;
;  Prereqs: Inno Setup 6  (https://jrsoftware.org/isinfo.php).
;  Build the plugin first (Release):
;      cmake -B build -DCMAKE_BUILD_TYPE=Release
;      cmake --build build --config Release
;  Then, from this folder:
;      "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" necronam-max.iss
;  Output lands in ..\..\dist\.
;
;  Override the version:  ISCC.exe /DMyAppVersion=1.2.3 necronam-max.iss
; ============================================================================

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif

#define MyAppName "NECRONAM MAX"
#define MyAppPublisher "CP Software"
; Artefacts are emitted by JUCE here (relative to this .iss file):
#define ArtRoot "..\..\build\NECRONAM_MAX_artefacts\Release"

[Setup]
AppId={{7F3A1C44-2D9B-4E6A-AB12-9C5E0D4F8A21}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppPublisher}\{#MyAppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Writing to Common Files\VST3 and Program Files needs elevation.
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=NECRONAM-MAX-{#MyAppVersion}-Windows
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
; --- VST3 (a bundle folder on Windows: NECRONAM MAX.vst3\Contents\...) ------
Source: "{#ArtRoot}\VST3\NECRONAM MAX.vst3\*"; \
    DestDir: "{commoncf}\VST3\NECRONAM MAX.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; --- Standalone app -------------------------------------------------------
Source: "{#ArtRoot}\Standalone\NECRONAM MAX.exe"; \
    DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\NECRONAM MAX.exe"; Components: standalone

[Run]
Filename: "{app}\NECRONAM MAX.exe"; Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent; Components: standalone
