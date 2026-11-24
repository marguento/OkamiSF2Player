; The Windows installer (Inno Setup 6): the VST3 into Common Files\VST3, with an uninstaller.
; Built by .github/workflows/windows.yml: iscc /DVersion=x.y.z /DPluginDir=<the built .vst3> /DOutDir=<dist>
; The AppId never changes: Windows finds an earlier install (to update or remove it) by it.

#ifndef Version
  #error Pass /DVersion=x.y.z
#endif
#ifndef PluginDir
  #error Pass /DPluginDir=<path to Okami SF2 Player.vst3>
#endif
#ifndef OutDir
  #define OutDir "dist"
#endif

[Setup]
AppId={{A53E7024-0204-4BC6-9B93-2CBF9322539E}
AppName=Okami SF2 Player
AppVersion={#Version}
AppVerName=Okami SF2 Player {#Version}
AppPublisher=Marguento
AppPublisherURL=https://github.com/marguento/OkamiSF2Player
AppSupportURL=https://github.com/marguento/OkamiSF2Player/issues
DefaultDirName={commoncf64}\VST3\Okami SF2 Player.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallDisplayName=Okami SF2 Player (VST3)
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputDir={#OutDir}
OutputBaseFilename=Okami-SF2-Player-{#Version}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
VersionInfoVersion={#Version}

[Files]
Source: "{#PluginDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
