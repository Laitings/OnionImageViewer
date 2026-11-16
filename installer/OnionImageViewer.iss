; Inno Setup script for OnionImageViewer. Don't compile this by hand: run build-installer.bat
; (or build-installer.ps1), which builds the app, collects Qt and the runtime into a staging
; folder and then compiles this script with the version and folders filled in.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
; Windows file versions must be plain numbers (1.2.0), so a tag like 1.2.0-beta passes 1.2.0 here.
#ifndef NumericVersion
  #define NumericVersion AppVersion
#endif
#ifndef StageDir
  #define StageDir "stage"
#endif
#ifndef OutputDir
  #define OutputDir "Output"
#endif

#define AppName "Onion Image Viewer"
#define AppExe "OnionImageViewer.exe"
#define ProgId "OnionImageViewer.Image"
; Official Ghostscript 10.08.0 installer (AGPL) from Artifex, checked against its SHA-256 before it runs.
#define GsFile "gs10080w64.exe"
#define GsUrl "https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs10080/gs10080w64.exe"
#define GsSha256 "52a91b8bf09298788d7a57b9206127026c23eacd75405f0a131e26dc381dce50"

[Setup]
; The AppId identifies the app for upgrades and uninstall. Never change it.
AppId={{DA2AC044-B386-4A28-AE45-4DDD297AC12A}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Onion
AppPublisherURL=https://github.com/Laitings/OnionImageViewer
DefaultDirName={autopf}\OnionImageViewer
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Installs for the current user without admin rights; the first page offers "all users" too.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=OnionImageViewer-Setup-{#AppVersion}
SetupIconFile=..\resources\icons\OnionImageViewer.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; A running viewer locks its files: offer to close it, and reopen it afterwards.
CloseApplications=yes
RestartApplications=yes
ChangesAssociations=yes
VersionInfoVersion={#NumericVersion}
VersionInfoDescription={#AppName} Setup

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "openwith"; Description: "Add {#AppName} to ""Open with"" for EXR, DPX, TIFF and other images"; GroupDescription: "Explorer:"
; Ghostscript draws EPS files. It is a separate, free program, so it is offered rather than bundled,
; and only when it isn't installed yet. See the [Code] section.
Name: "installgs"; Description: "Download and install Ghostscript (about 65 MB, needs administrator approval). Onion Image Viewer uses it to draw EPS files; without it an EPS shows only its embedded preview, if it has one"; GroupDescription: "Optional extra:"; Flags: unchecked; Check: GhostscriptMissing

[InstallDelete]
; Clear out files from an older version (Qt plugins and LUTs can change between versions).
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\luts"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\iconengines"

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; "Open with": registered alongside whatever app is the default, never replacing it.
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueData: "Image"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueData: """{app}\{#AppExe}"",0"; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.exr\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".exr"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.dpx\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".dpx"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.tif\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".tif"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.tiff\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".tiff"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.png\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".png"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.jpg\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".jpg"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.jpeg\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".jpeg"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.bmp\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".bmp"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.tga\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".tga"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.webp\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".webp"; ValueData: ""; Tasks: openwith

[Run]
Filename: "{tmp}\{#GsFile}"; Parameters: "/S"; StatusMsg: "Installing Ghostscript..."; Flags: shellexec waituntilterminated skipifdoesntexist; Verb: "runas"; Check: GhostscriptDownloaded
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  GsDownloadPage: TDownloadWizardPage;
  GsDownloaded: Boolean;

// True when Ghostscript is already there: its registry keys, its default install folder or the PATH.
function GhostscriptInstalled: Boolean;
var
  FindRec: TFindRec;
begin
  Result := RegKeyExists(HKLM64, 'SOFTWARE\Artifex\GPL Ghostscript')
         or RegKeyExists(HKLM32, 'SOFTWARE\Artifex\GPL Ghostscript')
         or (FileSearch('gswin64c.exe', GetEnv('PATH')) <> '')
         or (FileSearch('gswin32c.exe', GetEnv('PATH')) <> '');
  if not Result then
  begin
    if FindFirst(ExpandConstant('{commonpf64}\gs\gs*'), FindRec) then
    begin
      Result := True;
      FindClose(FindRec);
    end;
  end;
end;

function GhostscriptMissing: Boolean;
begin
  Result := not GhostscriptInstalled;
end;

function GhostscriptDownloaded: Boolean;
begin
  Result := GsDownloaded;
end;

procedure InitializeWizard;
begin
  GsDownloadPage := CreateDownloadPage('Downloading Ghostscript',
    'Ghostscript lets Onion Image Viewer draw EPS files.', nil);
end;

// Downloads the Ghostscript installer before the files are copied. A failed download only skips
// Ghostscript: the app itself still installs.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and WizardIsTaskSelected('installgs') then
  begin
    GsDownloadPage.Clear;
    GsDownloadPage.Add('{#GsUrl}', '{#GsFile}', '{#GsSha256}');
    GsDownloadPage.Show;
    try
      try
        GsDownloadPage.Download;
        GsDownloaded := True;
      except
        if not GsDownloadPage.AbortedByUser then
          SuppressibleMsgBox('Ghostscript could not be downloaded, so it will be skipped. '
            + 'You can install it later from ghostscript.com.' + #13#10#13#10 + GetExceptionMessage,
            mbError, MB_OK, IDOK);
      end;
    finally
      GsDownloadPage.Hide;
    end;
  end;
end;
