# OnionImageViewer

A fast image viewer and converter for Windows, built for film and VFX work. Browse folders like
in Explorer, view stills and frame sequences, check log footage through a LogC or LUT view, read
the full file metadata, and export what you see with the view burned in.

## Features

- **Formats**
  - Everything Qt reads: PNG, JPEG, BMP, GIF, and with the Qt Image Formats add-on TIFF, WebP, TGA and more.
  - **DPX**: own reader (`src/DpxReader.*`). RGB, RGBA, ABGR and luma, 8/10/12/16 bit, filled or packed, either endianness.
  - **OpenEXR**: through the OpenEXR library from vcpkg. Optional at build time.
  - **Photoshop** `.psd` / `.psb`: own reader (`src/PsdReader.*`). Uses the stored composite, or blends the layers when there is none.
  - **PDF and Illustrator** `.pdf` / `.ai`: rendered with the Qt PDF module. Optional at build time.
  - **EPS** `.eps` / `.epsf` / `.epsi`: rendered with Ghostscript when it is installed, otherwise the embedded preview is shown.
- **Color views** (Color menu): None, Auto, ARRI LogC3 → Rec.709 and LogC4 → Rec.709 (built-in approximations, or ARRI's own `.cube` LUTs when they are found in `luts/` next to the .exe), and any `.cube` LUT you load. EXR input can be set to ACES2065-1 or ACEScg for ACES comps.
- **File browser**: folder tree with quick locations, breadcrumb path bar, icon / list / details / tiles views, Windows shell thumbnails (no full downloads from OneDrive), Explorer-style multi-select and copy / cut / paste that works together with Explorer.
- **Viewer**: zoom and pan, channel isolation (R, G, B, A, luma), clipping overlay, compare wipe against a pinned image, sequence playback with read-ahead, full screen.
- **Metadata panel** (Ctrl+I): every EXR attribute, the full DPX header, or what Qt reports, with a filter.
- **Export As** (Ctrl+Shift+S): save the picture as shown, with the color view or LUT burned in.

View → Keyboard Shortcuts in the app lists all the keys.

## Requirements (Windows)

| What | Version | Notes |
| --- | --- | --- |
| Visual Studio | 2026 (2022 works too) | "Desktop development with C++" workload. Its bundled vcpkg is used for OpenEXR. |
| Qt | 6.11 `msvc2022_64` | Installed under `C:\Qt\6.11.x\msvc2022_64`. Add **Qt Image Formats** (TIFF, WebP, TGA) and **Qt PDF** (PDF/AI) in the Qt Maintenance Tool. |
| CMake + Ninja | 3.21+ | Both come with Visual Studio. |
| OpenEXR | from vcpkg | Declared in `vcpkg.json` (pinned baseline, triplet `x64-windows-static-md`, linked statically). Fetched on the first configure. |
| Ghostscript | optional, at run time | For EPS files. Found as `gswin64c` on the PATH or under `C:\Program Files\gs`. |
| Inno Setup 6 | optional | Only for building the installer: `winget install JRSoftware.InnoSetup`. |

Without OpenEXR or Qt PDF the app still builds; it just can't open those formats (CMake says so
when it configures).

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
- `-DONION_ARRI_LUT_DIR=Q:/Davinci_Shared_LUTs/Arri` copies ARRI's LogC3/LogC4 → Rec.709 `.cube` files into `luts/` next to the .exe, where the ARRI views use them instead of the built-in curves.
- Settings (view mode, color view, recent files, folders) are kept with `QSettings` under `OnionImageViewer` in the registry.

## Windows installer

Double-click `installer\build-installer.bat` (or run `installer\build-installer.ps1` with the same
`-QtDir`, `-ArriLutDir`, `-SkipBuild` options). It builds Release into `build-installer`,
collects the .exe, Qt (via `windeployqt`), the MSVC runtime and the ARRI LUTs in
`installer\stage`, and compiles `installer\OnionImageViewer.iss` with Inno Setup into
`installer\Output\OnionImageViewer-Setup-<version>.exe`. The version comes from
`project(OnionImageViewer VERSION x.y.z)` in `CMakeLists.txt`.

A GitHub Actions workflow (`.github/workflows/release-installer.yml`) builds the same installer
on Windows when a release is published and attaches it to the release. The release tag is the
version: tag `v1.2.0` gives `OnionImageViewer-Setup-1.2.0.exe`.

## Code layout

```
CMakeLists.txt            build; optional OpenEXR and Qt PDF; ARRI LUT copy
vcpkg.json                vcpkg manifest (OpenEXR)
build-run.bat             build + deploy + run (calls tools/build-run.ps1)
installer/                Inno Setup script and the scripts that build the installer
resources/                app icon (.ico/.png/.svg) and the Windows .rc file
src/
  main.cpp                QApplication setup, organisation/app name for QSettings
  MainWindow.*            the window: menus, actions, file grid, viewer page, thumbnails,
                          shortcuts, export, settings (the biggest file)
  FolderTreePanel.*       folder tree and quick locations on the left
  PathBar.*               breadcrumb path bar (Alt+D / F4 to type a path)
  FileItemDelegate.*      drawing of the Details and Tiles views
  FileOps.*               copy / cut / paste via the system clipboard, background file jobs
  ImageViewport.*         image display: zoom, pan, channels, clipping overlay
  OverlayButton.*         fading round buttons over the image (close, previous, next)
  Icons.*                 vector line icons for toolbar and menus
  FrameLoader.*           background loading, frame cache and read-ahead for sequences
  ImageLoader.*           picks the reader by file type (DPX, EXR, PSD, vector, else QImageReader)
  DpxReader.*             DPX reader
  ExrReader.*             EXR reader (needs ONION_HAVE_OPENEXR)
  PsdReader.*             PSD/PSB reader
  VectorReader.*          PDF / AI (Qt PDF) and EPS (Ghostscript) rendering
  ColorTransform.*        display transforms: LogC3/LogC4 → Rec.709, .cube LUTs, ACES input
  Metadata.*              header-only metadata for the metadata panel
  MetadataPanel.*         the metadata side panel
```

Readers return images with their stored values (DPX as 16-bit code values, EXR as half floats);
`ColorTransform::toDisplay()` turns them into an 8-bit image for the screen.
