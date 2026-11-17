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
Name: "openwith"; Description: "Add {#AppName} to ""Open with"" for EXR, DPX, TIFF, camera raw and other images"; GroupDescription: "Explorer:"

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
Root: HKA; Subkey: "Software\Classes\.dng\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".dng"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.arw\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".arw"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.cr2\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".cr2"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.cr3\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".cr3"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.nef\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".nef"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.nrw\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".nrw"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.raf\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".raf"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.orf\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".orf"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.rw2\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".rw2"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.pef\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".pef"; ValueData: ""; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\.srw\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".srw"; ValueData: ""; Tasks: openwith

[Run]
Filename: "{tmp}\{#GsFile}"; Parameters: "/S"; StatusMsg: "Installing Ghostscript..."; Flags: shellexec waituntilterminated skipifdoesntexist; Verb: "runas"; Check: GhostscriptDownloaded
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  GsPage: TWizardPage;
  GsCheck: TNewCheckBox;
  GsDownloadPage: TDownloadWizardPage;
  GsDownloaded: Boolean;

// True when a Ghostscript program is really there: on the PATH or in a default install folder.
// The registry is not used: Ghostscript's uninstaller leaves an empty "GPL Ghostscript" key behind.
function GhostscriptInGsFolder(const Root: String): Boolean;
var
  FindRec: TFindRec;
  Bin: String;
begin
  Result := False;
  if FindFirst(Root + '\gs\gs*', FindRec) then
  begin
    try
      repeat
        Bin := Root + '\gs\' + FindRec.Name + '\bin\';
        if FileExists(Bin + 'gswin64c.exe') or FileExists(Bin + 'gswin32c.exe') then
        begin
          Result := True;
          Break;
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

function GhostscriptInstalled: Boolean;
begin
  Result := (FileSearch('gswin64c.exe', GetEnv('PATH')) <> '')
         or (FileSearch('gswin32c.exe', GetEnv('PATH')) <> '')
         or GhostscriptInGsFolder(ExpandConstant('{commonpf64}'))
         or GhostscriptInGsFolder(ExpandConstant('{commonpf32}'));
end;

function GhostscriptMissing: Boolean;
begin
  Result := not GhostscriptInstalled;
end;

function GhostscriptDownloaded: Boolean;
begin
  Result := GsDownloaded;
end;

// Ghostscript draws EPS files. It is a separate, free program, so it is offered on a page of its own
// (only when it isn't installed yet) instead of being bundled.
procedure InitializeWizard;
var
  Info: TNewStaticText;
begin
  GsPage := CreateCustomPage(wpSelectTasks, 'Optional: Ghostscript', 'Needed only to view EPS files');
  Info := TNewStaticText.Create(GsPage);
  Info.Parent := GsPage.Surface;
  Info.WordWrap := True;
  Info.AutoSize := True;
  Info.Width := GsPage.SurfaceWidth;
  Info.Caption :=
    'EPS (Encapsulated PostScript) is an older vector graphics format used for logos and illustrations '
    + 'from design programs such as Adobe Illustrator.' + #13#10#13#10
    + 'Onion Image Viewer needs a free program called Ghostscript to draw EPS files, and it is not '
    + 'installed on this PC. Without it, an EPS file only shows its embedded preview, if it has one.'
    + #13#10#13#10
    + 'Ghostscript is about 65 MB, and installing it asks for administrator approval.';
  GsCheck := TNewCheckBox.Create(GsPage);
  GsCheck.Parent := GsPage.Surface;
  GsCheck.Top := Info.Top + Info.Height + ScaleY(16);
  GsCheck.Width := GsPage.SurfaceWidth;
  GsCheck.Caption := 'Download and install Ghostscript';
  GsDownloadPage := CreateDownloadPage('Downloading Ghostscript',
    'Ghostscript lets Onion Image Viewer draw EPS files.', nil);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = GsPage.ID) and not GhostscriptMissing;
end;

// Downloads the Ghostscript installer before the files are copied. A failed download only skips
// Ghostscript: the app itself still installs.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpReady) and GsCheck.Checked and GhostscriptMissing then
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

// Lists the Ghostscript download on the "Ready to Install" page, next to the tasks.
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo, MemoComponentsInfo,
  MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := '';
  if MemoDirInfo <> '' then
    Result := Result + MemoDirInfo + NewLine + NewLine;
  if MemoGroupInfo <> '' then
    Result := Result + MemoGroupInfo + NewLine + NewLine;
  if MemoTasksInfo <> '' then
    Result := Result + MemoTasksInfo + NewLine + NewLine;
  if GsCheck.Checked and GhostscriptMissing then
    Result := Result + 'Ghostscript (for EPS files):' + NewLine
      + Space + 'Download (about 65 MB) and install Ghostscript' + NewLine;
end;
