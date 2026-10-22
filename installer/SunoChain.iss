; Suno Chain - Windows installer (Inno Setup 6). Built by GitHub Actions:
;   ISCC /DAppVersion=2.0.3 /DBundleDir=<path to "Suno Chain.vst3"> /DOutDir=<dir> installer\SunoChain.iss
; Installs (or replaces) the VST3 in C:\Program Files\Common Files\VST3. The factory presets are written by the
; plugin itself into Documents\SunoChain\Presets the first time it loads (your own presets are never touched).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BundleDir
  #define BundleDir "..\build\SunoChain_artefacts\Release\VST3\Suno Chain.vst3"
#endif
#ifndef OutDir
  #define OutDir "..\build\installer"
#endif

[Setup]
AppId={{6F3C2B7A-4E1D-4C59-9A8E-5B2D7C1E9F40}
AppName=Suno Chain
AppVersion={#AppVersion}
AppVerName=Suno Chain {#AppVersion}
AppPublisher=Pouria Motabean
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UsePreviousAppDir=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutDir}
OutputBaseFilename=SunoChain_Setup_v{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Suno Chain {#AppVersion}
UninstallDisplayIcon={sys}\shell32.dll,138
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Suno Chain VST3 installer

[InstallDelete]
; a clean replace: the old bundle goes first, so no stale file from an older version survives
Type: filesandordirs; Name: "{commoncf64}\VST3\Suno Chain.vst3"

[Files]
Source: "{#BundleDir}\*"; DestDir: "{commoncf64}\VST3\Suno Chain.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Suno Chain.vst3"

[Code]
// A host that has the plugin loaded keeps the file locked: ask to close Cubase (or any DAW) first.
function HostRunning(): String;
var
  Locator, Service, Procs, P: Variant;
  I: Integer;
  N: String;
begin
  Result := '';
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Procs := Service.ExecQuery('SELECT Name FROM Win32_Process');
    for I := 0 to Procs.Count - 1 do
    begin
      P := Procs.ItemIndex(I);
      N := Lowercase(P.Name);
      if (Pos('cubase', N) = 1) or (Pos('nuendo', N) = 1) or (Pos('ableton', N) = 1) or (N = 'reaper.exe')
         or (Pos('fl64', N) = 1) or (Pos('studio one', N) = 1) or (Pos('vstbridge', N) > 0) then
      begin
        Result := P.Name;
        Exit;
      end;
    end;
  except
    Result := '';   // WMI unavailable: Inno's own "files in use" check still applies
  end;
end;

function InitializeSetup(): Boolean;
var
  Host: String;
begin
  Result := True;
  Host := HostRunning();
  while Host <> '' do
  begin
    if MsgBox(Host + ' is running.' + #13#10 + 'Close it (save your project first), then press Retry.' + #13#10 +
              'The plugin cannot be replaced while a host has it loaded.', mbError, MB_RETRYCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
    Host := HostRunning();
  end;
end;
