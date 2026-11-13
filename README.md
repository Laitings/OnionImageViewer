# OnionImageViewer

A fast image viewer and converter for Windows, built for film and VFX work. Browse folders like
in Explorer, view stills and frame sequences, check log footage through a LogC or LUT view, read
the full file metadata, and export what you see with the view burned in.

## Features

- **Formats**
  - Everything Qt reads: PNG, JPEG, BMP, GIF (including animation), and with the Qt Image Formats add-on TIFF, TGA and more.
  - **WebP** `.webp`: through libwebp from vcpkg (linked statically), so it works without the Qt Image Formats add-on. Lossy, lossless and alpha; animated WebP plays like a GIF; embedded ICC profiles are converted to sRGB; Exif shows in the metadata panel. Optional at build time (without libwebp, Qt's WebP plugin is used if installed).
  - **DPX**: own reader (`src/DpxReader.*`). RGB, RGBA, ABGR and luma, 8/10/12/16 bit, filled or packed, either endianness.
  - **OpenEXR**: through the OpenEXR library from vcpkg. Optional at build time.
  - **Digital Negative** `.dng`: LibRaw develops the raw image at full resolution in sRGB. Folder thumbnails use the embedded preview when available, or a reduced raw render. Optional at build time.
  - **HEIF / HEIC** `.heic` / `.heif` / `.hif` (iPhone photos, Canon and Fuji HEIF): through libheif with the libde265 HEVC decoder from vcpkg, as DLLs (both are LGPL). 8 and 10/12 bit, rotation and crop applied, Display P3 converted to sRGB, embedded thumbnails used for the file browser. Optional at build time.
  - **Photoshop** `.psd` / `.psb`: own reader (`src/PsdReader.*`). Uses the stored composite, or blends the layers when there is none.
  - **PDF and Illustrator** `.pdf` / `.ai`: rendered with the Qt PDF module. Optional at build time.
  - **EPS** `.eps` / `.epsf` / `.epsi`: rendered with Ghostscript when it is installed, otherwise the embedded preview is shown.
- **Color views** (Color menu): None, Auto, ARRI LogC3 → Rec.709 and LogC4 → Rec.709 (built-in approximations, or ARRI's own `.cube` LUTs when they are found in `luts/` next to the .exe), and any `.cube` LUT you load. EXR input can be set to ACES2065-1 or ACEScg for ACES comps.
- **File browser**: folder panel laid out like Explorer's navigation pane, with Quick access (pinned folders: pin and unpin from the right-click menus, drag to reorder; starts with OneDrive, Desktop, Downloads, Documents and Pictures) above This PC's drive tree, breadcrumb path bar, icon / list / details / tiles views, Windows shell thumbnails (no full downloads from OneDrive), Explorer-style multi-select and copy / cut / paste that works together with Explorer.
- **Viewer**: zoom and pan, channel isolation (R, G, B, A, luma), clipping overlay, compare wipe against a pinned image, full screen.
- **Image sequence player**: any format the viewer opens can be a sequence (DPX, EXR, TIFF, PNG, JPEG, …): files in one folder whose names end in a frame number before the extension (`A001_C002.1001.dpx`, `shot_0042.exr`). Open any frame and a player bar appears under the image with first / previous / play / next / last buttons, a scrub bar (click or drag), the frame number and position (`1042  42 / 200`), and the playback speed: 12, 15, 23.976, 24, 25, 29.97, 30, 48, 50, 59.94 or 60 fps, or Custom… for any rate. Loop starts again after the last frame. **Real time** keeps to the chosen rate by skipping frames that don't load in time; with it off, every frame is shown and playback slows down when the disk can't keep up. The rate playback actually reaches is shown next to it (amber when it falls behind). Frames play in frame-number order even without zero padding, and other files in the folder don't get in the way. Space plays and pauses, Left/Right step a frame, Home/End jump to the first and last frame; stepping past either end moves on to the folder's other images. The color view or LUT, channels, clipping and compare wipe apply while playing, with frames loaded and rendered ahead on background threads. The speed, loop and real-time settings are remembered; View → Player Controls hides the bar. Outside a sequence, Space plays through the folder's images at the same speed.
- **Split viewer** (V, or View → Split Viewer): splits the viewer into two panes, side by side or top and bottom. The second pane has its own file grid with a path bar, an Up button and its own search field (same wildcards as the main one), so you can pick any image from any folder to show next to the main one. Side by side, its bar sits on the address bar row, lined up over the pane. Click a pane or press Tab to make it active (it gets a blue frame); the arrow keys, Home/End, F and 1 act on the active pane, and Esc in the second pane goes back to its grid. Sync Zoom and Pan (on by default) zooms and pans both panes together; images of different sizes are shown at the same width so the same part of the frame lines up. The color view, channels and clipping overlay apply to both panes, and the status bar shows the active pane's image. The metadata panel, the scopes, Copy Path and Show in Explorer follow the active pane (in the second pane's grid, the file under the cursor).
- **Scopes** (S in the viewer, Ctrl+Shift+H anywhere): a histogram (RGB or luma) and a waveform (luma, RGB parade or RGB overlay) of the viewed image in a dockable panel, with the hovered pixel marked on both. By default they read the picture as displayed, with the color view or LUT applied, matching the pixel readout's display values (0–100 scale). **Stored values** reads the file's own values instead: scene-linear float images (EXR, ACES) get a scale in stops with 18% grey marked, other float images extend above 1 when they go there. Computed on a background thread from a downsampled image, and only while the panel is open.
- **Metadata panel** (Ctrl+I): every EXR attribute, the full DPX header, HEIF and WebP details with the Exif data (camera, lens, exposure, date, GPS), or what Qt reports, with a filter.
- **Export As** (Ctrl+Shift+S): save the picture as shown, with the color view or LUT burned in. For an animated GIF, this saves the frame shown when you open the export dialog.
- **Batch Convert**: select images in the file grid and choose File → Batch Convert or use the right-click menu. Pick an output folder and format (PNG, JPEG, TIFF, WebP or BMP, depending on installed Qt plugins). JPEG and WebP offer a quality setting. Keep the original size or set an output width and height and choose center crop, scale with crop, fit with black padding, or stretch. Conversion uses the default display rendering without the active color view or LUT, and uses the first frame of animated images. Existing files are kept by numbering new outputs when names collide.
- **Batch Rename** (F2): select files or folders in the grid and choose File → Batch Rename or Rename… / Batch Rename… in the right-click menu. Build the new name from a pattern with tokens: `{name}` (old name), `{n}` (counter with start, step and digits; `{n:4}` sets the digits), `{date}` (Exif date taken from JPEG, TIFF/DNG and HEIC, else the file date; `{date:yyyyMMdd_HHmmss}` sets the format), `{modified}` (file date), `{frame}`, `{folder}` and `{ext}`. Then find and replace (plain text or a regular expression with `\1` groups) and change the case of the name and the extension. The extension itself is always kept. Image sequences keep their frame numbers: `A001_C002.1001.dpx` becomes `Shot_010.1001.dpx`, and a whole sequence takes one counter value. A live preview shows every old → new name and marks names that can't be used (already taken, the same for two files, characters or names Windows doesn't allow), and Rename stays off until there are none. Swaps and case-only changes work. If one rename fails, the whole batch is put back. Edit → Undo Rename (Ctrl+Z) restores the names of the last batch; press it again to step back through earlier batches.

View → Keyboard Shortcuts in the app lists all the keys.

On startup, the app checks GitHub for a newer stable release and offers to open its download page if one is available. Check again from Help → Check for Updates or from the About dialog.

Use the search field beside the path bar (Ctrl+F) to filter the current folder by name as you type. Plain text matches anywhere in the name. Wildcards work like in Explorer: `*` stands for any run of characters and `?` for one character, so `*.psd` shows only PSD files. Separate several patterns with `;` or spaces (`*.psd;*.tif`) to show files matching any of them. Matching ignores case. Clear it with the field's clear button or Esc while the field has focus.
Turn on **Images only** to hide unsupported files while keeping folders available for navigation. The app remembers the setting. Right-click an unsupported file and choose **Open in Default App** to open it using the system file association.

## Requirements (Windows)

| What | Version | Notes |
| --- | --- | --- |
| Visual Studio | 2026 (2022 works too) | "Desktop development with C++" workload. Its bundled vcpkg is used for OpenEXR. |
| Qt | 6.11 `msvc2022_64` | Installed under `C:\Qt\6.11.x\msvc2022_64`. Qt Network comes with the base package and powers update checks. Add **Qt Image Formats** (TIFF, TGA; also needed to *write* WebP in Batch Convert) and **Qt PDF** (PDF/AI) in the Qt Maintenance Tool. |
| CMake + Ninja | 3.21+ | Both come with Visual Studio. |
| OpenEXR | from vcpkg | Declared in `vcpkg.json` (pinned baseline, triplet `x64-windows-onion` from `triplets/`, linked statically). Fetched on the first configure. |
| LibRaw | from vcpkg | DNG decoding with the `dng-lossy` feature. Ships as `raw_r.dll` with its license in `licenses/`. |
| libheif + libde265 | from vcpkg | Declared in `vcpkg.json` without libheif's default features (its `hevc` feature is the GPL x265 encoder). Built as DLLs (LGPL) by the `triplets/` triplet and copied next to the .exe with their licences in `licenses/`. |
| libwebp | from vcpkg | WebP decoding and animation (BSD licence, linked statically). |
| Ghostscript | optional, at run time | For EPS files. Found as `gswin64c` on the PATH or under `C:\Program Files\gs`. |
| Inno Setup 6 | optional | Only for building the installer: `winget install JRSoftware.InnoSetup`. |

Without OpenEXR, LibRaw, libheif, libwebp or Qt PDF the app still builds; it just can't open those formats (CMake
says so when it configures). The first configure after HEIF support was added builds libheif and
rebuilds OpenEXR for the new triplet, which takes a few minutes; `build-run.bat` reconfigures an
older `build-msvc` folder by itself.

## Build and run

The easy way, from any command prompt in the repository folder:

```bat
build-run.bat
```

`build-run.bat` (which runs `tools\build-run.ps1`) sets up the Visual Studio x64 environment,
picks the newest `C:\Qt\6.*\msvc2022_64`, closes a copy of the viewer that is running from the
build folder, configures `build-msvc` the first time, builds Release, runs `windeployqt` and
starts the app. Options: `-QtDir C:\Qt\6.11.0\msvc2022_64`, `-ArriLutDir <folder>`, `-NoRun`.

By hand, in the **x64 Native Tools Command Prompt for VS 2026** (the plain developer prompt
builds x86):

```bat
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
cmake --build build-msvc
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe --release --no-translations build-msvc\OnionImageViewer.exe
build-msvc\OnionImageViewer.exe
```

Notes:

- **Close the app before rebuilding.** A running `OnionImageViewer.exe` locks the file and the link step fails.
- vcpkg is picked up automatically from `VCPKG_ROOT`, or else from Visual Studio (`%VSINSTALLDIR%\VC\vcpkg`). It has to be set up when the build folder is first configured; a build folder made without it must be deleted and configured again.
- ARRI's LogC3/LogC4 → Rec.709 `.cube` files from `resources/luts/arri` are copied into `luts/` next to the .exe, where the ARRI views use them instead of the built-in curves. `-DONION_ARRI_LUT_DIR=Q:/Davinci_Shared_LUTs/Arri` takes them from another folder.
- `-DONION_VERSION=1.2.0` sets the version the app shows (default: the CMake project version).
- Settings (view mode, color view, recent files, folders, pinned folders, scope choices) are kept with `QSettings` under `OnionImageViewer` in the registry.

## Windows installer

Double-click `installer\build-installer.bat` (or run `installer\build-installer.ps1` with the same
`-QtDir`, `-ArriLutDir`, `-SkipBuild` options). It builds Release into `build-installer`,
collects the .exe, the libheif/libde265 and LibRaw DLLs with their licences, Qt (via `windeployqt`), the
MSVC runtime and the ARRI LUTs in
`installer\stage`, and compiles `installer\OnionImageViewer.iss` with Inno Setup into
`installer\Output\OnionImageViewer-Setup-<version>.exe`. The version comes from
`project(OnionImageViewer VERSION x.y.z)` in `CMakeLists.txt` unless `-Version` is given.
ARRI's LogC3/LogC4 → Rec.709 LUTs come from `resources\luts\arri` (`-ArriLutDir` uses another
folder instead).

### Installer on GitHub releases

Publishing a GitHub release builds the installer automatically
(`.github/workflows/release-installer.yml`): on GitHub, Releases → Draft a new release, pick a tag
such as `v1.2.0` and publish. The workflow builds the app with Qt 6.11 (Image Formats, Qt PDF),
OpenEXR, libheif and LibRaw on Windows, and about 10 minutes later `OnionImageViewer-Setup-1.2.0.exe` is attached to
the release. The tag (without the `v`) becomes the version everywhere: the About box, the .exe's
file properties and the installer. It can also be run by hand from Actions → Release installer →
Run workflow; that gives a test installer as a download on the run, with no release.

## Code layout

```
CMakeLists.txt            build; optional image format libraries; ARRI LUT copy
vcpkg.json                vcpkg manifest (OpenEXR, libheif, LibRaw, libwebp)
triplets/                 vcpkg triplet: static libraries, but libheif/libde265 and LibRaw as DLLs (LGPL)
build-run.bat             build + deploy + run (calls tools/build-run.ps1)
installer/                Inno Setup script and the scripts that build the installer
resources/                app icon (.ico/.png/.svg) and the Windows .rc file
src/
  main.cpp                QApplication setup, organisation/app name for QSettings
  MainWindow.*            the window: menus, actions, file grid, viewer page, thumbnails,
                          shortcuts, export, settings (the biggest file)
  FolderTreePanel.*       folder panel on the left: Quick access pins and the drive tree
  PathBar.*               breadcrumb path bar (Alt+D / F4 to type a path)
  FileItemDelegate.*      drawing of the Details and Tiles views
  FileOps.*               copy / cut / paste via the system clipboard, background file jobs
  BatchRename.*           batch renamer engine: pattern tokens, sequences, conflict checks, safe renames
  BatchRenameDialog.*     Batch Rename dialog with the live old → new preview
  ImageViewport.*         image display: zoom, pan, channels, clipping overlay
  SplitPane.*             second pane of the split viewer: its own file grid, viewport and frame loader
  PlayerBar.*             transport bar for image sequences: play controls, scrub bar, frame counter, fps
  Sequence.*              finds the image sequence a file belongs to (frame number in the name)
  NameFilter.h            name / wildcard matching shared by the search fields
  OverlayButton.*         fading round buttons over the image (close, previous, next)
  Icons.*                 vector line icons for toolbar and menus
  FrameLoader.*           background loading, frame cache and read-ahead for sequences; GIF/WebP playback
  ImageLoader.*           picks the reader by file type (DPX, EXR, HEIF, WebP, DNG, PSD, vector, else QImageReader)
  DpxReader.*             DPX reader
  ExrReader.*             EXR reader (needs ONION_HAVE_OPENEXR)
  HeifReader.*            HEIF/HEIC reader (needs ONION_HAVE_LIBHEIF)
  WebpReader.*            WebP reader and animated WebP playback (needs ONION_HAVE_LIBWEBP)
  DngReader.*             DNG raw development and embedded previews (needs ONION_HAVE_LIBRAW)
  Exif.*                  Exif parser for the metadata panel and the renamer's date taken
  PsdReader.*             PSD/PSB reader
  VectorReader.*          PDF / AI (Qt PDF) and EPS (Ghostscript) rendering
  ColorTransform.*        display transforms: LogC3/LogC4 → Rec.709, .cube LUTs, ACES input
  Metadata.*              header-only metadata for the metadata panel
  MetadataPanel.*         the metadata side panel
  Scopes.*                histogram and waveform computation (runs on a background thread)
  ScopesPanel.*           the scopes side panel: drawing, source choice, hovered-pixel marks
```

Readers return images with their stored values (DPX as 16-bit code values, EXR as half floats);
`ColorTransform::toDisplay()` turns them into an 8-bit image for the screen.
