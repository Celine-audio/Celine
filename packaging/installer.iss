; The Windows installer, built by Inno Setup in .github/workflows/windows.yml:
;
;     iscc packaging\installer.iss
;
; after the build, with PROJECT_NAME, PRODUCT_NAME and COMPANY_NAME in the environment (CI
; reads them from the .env CMake writes). Paths here are relative to this file, so the build
; is expected in ..\Builds, as CI does it.
;
; Every format is a component, all ticked, so the default is "install everything" and a
; custom install is one untick away. The plugins go where hosts look for them system-wide;
; the standalone app goes under Program Files with a Start menu entry.
;
; The artwork in windows\ is drawn by make-artwork.swift; the license page is the AGPL
; itself, which is the licence these binaries are under.

#define Version Trim(FileRead(FileOpen("..\VERSION")))
#define ProjectName GetEnv('PROJECT_NAME')
#define ProductName GetEnv('PRODUCT_NAME')
#define Publisher GetEnv('COMPANY_NAME')
#define Year GetDateTimeString("yyyy", "", "")
#define Artefacts "..\Builds\" + ProjectName + "_artefacts\Release"

[Setup]
; Never change the AppId: it is how Windows knows a new version replaces an old one rather
; than installing beside it.
AppId={{F60F6A1B-B275-4F4C-9409-C9241FD85372}
AppName={#ProductName}
AppVersion={#Version}
AppVerName={#ProductName} {#Version}
AppPublisher={#Publisher}
AppPublisherURL=https://github.com/Celine-audio/Celine
AppSupportURL=https://github.com/Celine-audio/Celine/issues
AppUpdatesURL=https://github.com/Celine-audio/Celine/releases
AppCopyright=Copyright (C) {#Year} {#Publisher}

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The plugin folders are under Common Files, which every user's host reads.
PrivilegesRequired=admin

DefaultDirName={commonpf64}\{#Publisher}\{#ProductName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName={#ProductName}

LicenseFile=..\COPYING

WizardStyle=modern
WizardImageFile=windows\wizard-100.bmp,windows\wizard-150.bmp,windows\wizard-200.bmp
WizardSmallImageFile=windows\wizard-small-100.bmp,windows\wizard-small-150.bmp,windows\wizard-small-200.bmp
WizardImageStretch=no
SetupIconFile=windows\icon.ico
UninstallDisplayIcon={uninstallexe}
UninstallDisplayName={#ProductName}

OutputDir=..\Builds
OutputBaseFilename={#ProductName}-{#Version}-Windows-Setup
Compression=lzma2/ultra64
SolidCompression=yes

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "clap"; Description: "CLAP plugin"; Types: full custom
Name: "lv2"; Description: "LV2 plugin"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Put a shortcut to the standalone app on the desktop"; Components: standalone; Flags: unchecked

[Files]
; The linker leaves symbol and incremental-link files beside the plugins; none is any use
; to someone installing them.
Source: "{#Artefacts}\VST3\{#ProductName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#ProductName}.vst3"; Excludes: "*.ilk,*.pdb,*.lib,*.exp"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
Source: "{#Artefacts}\CLAP\{#ProductName}.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap
Source: "{#Artefacts}\LV2\{#ProductName}.lv2\*"; DestDir: "{commoncf64}\LV2\{#ProductName}.lv2"; Excludes: "*.ilk,*.pdb,*.lib,*.exp"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: lv2
Source: "{#Artefacts}\Standalone\{#ProductName}.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{autoprograms}\{#ProductName}"; Filename: "{app}\{#ProductName}.exe"; Components: standalone
Name: "{autodesktop}\{#ProductName}"; Filename: "{app}\{#ProductName}.exe"; Components: standalone; Tasks: desktopicon

[Run]
Filename: "{app}\{#ProductName}.exe"; Description: "Open {#ProductName} now"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone

[Messages]
FinishedLabel=Setup has installed [name] on your computer.%n%nRestart your DAW and rescan its plugins if [name] does not show up.
