# OnionImageViewer

A fast image viewer and converter for Windows, built for film and VFX work. Browse folders like
in Explorer, view stills and frame sequences, check log footage through a LogC or LUT view, read
the full file metadata, and export what you see with the view burned in.

## Features

- **Formats**
  - Everything Qt reads: PNG, JPEG, BMP, GIF (including animation), and with the Qt Image Formats add-on TIFF, WebP, TGA and more.
  - **DPX**: own reader (`src/DpxReader.*`). RGB, RGBA, ABGR and luma, 8/10/12/16 bit, filled or packed, either endianness.
  - **OpenEXR**: through the OpenEXR library from vcpkg. Optional at build time.
  - **Digital Negative** `.dng`: LibRaw develops the raw image at full resolution in sRGB. Folder thumbnails use the embedded preview when available, or a reduced raw render. Optional at build time.
  - **HEIF / HEIC** `.heic` / `.heif` / `.hif` (iPhone photos, Canon and Fuji HEIF): through libheif with the libde265 HEVC decoder from vcpkg, as DLLs (both are LGPL). 8 and 10/12 bit, rotation and crop applied, Display P3 converted to sRGB, embedded thumbnails used for the file browser. Optional at build time.
  - **Photoshop** `.psd` / `.psb`: own reader (`src/PsdReader.*`). Uses the stored composite, or blends the layers when there is none.
  - **PDF and Illustrator** `.pdf` / `.ai`: rendered with the Qt PDF module. Optional at build time.
  - **EPS** `.eps` / `.epsf` / `.epsi`: rendered with Ghostscript when it is installed, otherwise the embedded preview is shown.
- **Color views** (Color menu): None, Auto, ARRI LogC3 → Rec.709 and LogC4 → Rec.709 (built-in approximations, or ARRI's own `.cube` LUTs when they are found in `luts/` next to the .exe), and any `.cube` LUT you load. EXR input can be set to ACES2065-1 or ACEScg for ACES comps.
- **File browser**: folder panel laid out like Explorer's navigation pane, with Quick access (pinned folders: pin and unpin from the right-click menus, drag to reorder; starts with OneDrive, Desktop, Downloads, Documents and Pictures) above This PC's drive tree, breadcrumb path bar, icon / list / details / tiles views, Windows shell thumbnails (no full downloads from OneDrive), Explorer-style multi-select and copy / cut / paste that works together with Explorer.
- **Viewer**: zoom and pan, channel isolation (R, G, B, A, luma), clipping overlay, compare wipe against a pinned image, sequence playback with read-ahead, full screen.
- **Metadata panel** (Ctrl+I): every EXR attribute, the full DPX header, HEIF details with the Exif data (camera, lens, exposure, date, GPS), or what Qt reports, with a filter.
- **Export As** (Ctrl+Shift+S): save the picture as shown, with the color view or LUT burned in. For an animated GIF, this saves the frame shown when you open the export dialog.

View → Keyboard Shortcuts in the app lists all the keys.

On startup, the app checks GitHub for a newer stable release and offers to open its download page if one is available. Check again from Help → About OnionImageViewer → Check for Updates.

Use the search field beside the path bar (Ctrl+F) to filter the current folder by name as you type. Clear it with the field's clear button or Esc while the field has focus.

## Requirements (Windows)

| What | Version | Notes |
| --- | --- | --- |
| Visual Studio | 2026 (2022 works too) | "Desktop development with C++" workload. Its bundled vcpkg is used for OpenEXR. |
| Qt | 6.11 `msvc2022_64` | Installed under `C:\Qt\6.11.x\msvc2022_64`. Qt Network comes with the base package and powers update checks. Add **Qt Image Formats** (TIFF, WebP, TGA) and **Qt PDF** (PDF/AI) in the Qt Maintenance Tool. |
| CMake + Ninja | 3.21+ | Both come with Visual Studio. |
| OpenEXR | from vcpkg | Declared in `vcpkg.json` (pinned baseline, triplet `x64-windows-onion` from `triplets/`, linked statically). Fetched on the first configure. |
| LibRaw | from vcpkg | DNG decoding with the `dng-lossy` feature. Ships as `raw_r.dll` with its license in `licenses/`. |
| libheif + libde265 | from vcpkg | Declared in `vcpkg.json` without libheif's default features (its `hevc` feature is the GPL x265 encoder). Built as DLLs (LGPL) by the `triplets/` triplet and copied next to the .exe with their licences in `licenses/`. |
| Ghostscript | optional, at run time | For EPS files. Found as `gswin64c` on the PATH or under `C:\Program Files\gs`. |
| Inno Setup 6 | optional | Only for building the installer: `winget install JRSoftware.InnoSetup`. |

Without OpenEXR, LibRaw, libheif or Qt PDF the app still builds; it just can't open those formats (CMake
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
- Settings (view mode, color view, recent files, folders, pinned folders) are kept with `QSettings` under `OnionImageViewer` in the registry.

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
vcpkg.json                vcpkg manifest (OpenEXR, libheif, LibRaw)
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
  ImageViewport.*         image display: zoom, pan, channels, clipping overlay
  OverlayButton.*         fading round buttons over the image (close, previous, next)
  Icons.*                 vector line icons for toolbar and menus
  FrameLoader.*           background loading, frame cache and read-ahead for sequences
  ImageLoader.*           picks the reader by file type (DPX, EXR, HEIF, DNG, PSD, vector, else QImageReader)
  DpxReader.*             DPX reader
  ExrReader.*             EXR reader (needs ONION_HAVE_OPENEXR)
  HeifReader.*            HEIF/HEIC reader (needs ONION_HAVE_LIBHEIF)
  DngReader.*             DNG raw development and embedded previews (needs ONION_HAVE_LIBRAW)
  Exif.*                  Exif parser for the metadata panel
  PsdReader.*             PSD/PSB reader
  VectorReader.*          PDF / AI (Qt PDF) and EPS (Ghostscript) rendering
  ColorTransform.*        display transforms: LogC3/LogC4 → Rec.709, .cube LUTs, ACES input
  Metadata.*              header-only metadata for the metadata panel
  MetadataPanel.*         the metadata side panel
```

Readers return images with their stored values (DPX as 16-bit code values, EXR as half floats);
`ColorTransform::toDisplay()` turns them into an 8-bit image for the screen.
