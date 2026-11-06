; The Windows installer, built with Inno Setup. CI passes the version:
;   ISCC /DAppVersion=1.0.0 installer\PartialResynth.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; Identifies the product for upgrades and uninstalling, so it must never change.
AppId={{F0F9A7A3-D610-4A51-A25E-5C5C3DDD65AC}
AppName=PartialResynth
AppVersion={#AppVersion}
AppVerName=PartialResynth {#AppVersion}
AppPublisher=Hadronyche
AppPublisherURL=https://github.com/HadronycheAudio/PartialResynth
AppCopyright=Copyright (c) 2026 Hadronyche
LicenseFile=..\LICENSE
MinVersion=10.0

; The standard location for VST 3 plug-ins, which hosts scan by default, so there's nothing to choose.
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Keeps the uninstaller out of the folder that hosts scan.
UninstallFilesDir={commonpf64}\Hadronyche\PartialResynth
UninstallDisplayName=PartialResynth

OutputDir=..\build\installer
OutputBaseFilename=PartialResynth-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Dirs]
; Makes Windows show the bundle's icon, which desktop.ini sets.
Name: "{app}\PartialResynth.vst3"; Attribs: system

[Files]
Source: "..\build\VST3\Release\PartialResynth.vst3\*"; DestDir: "{app}\PartialResynth.vst3"; Flags: recursesubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}\PartialResynth.vst3\Contents\Resources\Licenses"; DestName: "PartialResynth-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\vst3sdk\LICENSE.txt"; DestDir: "{app}\PartialResynth.vst3\Contents\Resources\Licenses"; DestName: "VST3-SDK-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\vst3sdk\vstgui4\LICENSE"; DestDir: "{app}\PartialResynth.vst3\Contents\Resources\Licenses"; DestName: "VSTGUI-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\hadronychecore\LICENSE"; DestDir: "{app}\PartialResynth.vst3\Contents\Resources\Licenses"; DestName: "HadronycheCore-LICENSE.txt"; Flags: ignoreversion
Source: "..\licenses\pure-data.txt"; DestDir: "{app}\PartialResynth.vst3\Contents\Resources\Licenses"; DestName: "Pure-Data-LICENSE.txt"; Flags: ignoreversion
Source: "NOTICE.txt"; DestDir: "{app}\PartialResynth.vst3\Contents\Resources\Licenses"; Flags: ignoreversion
