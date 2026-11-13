# Instructions for coding agents

Read `README.md` first: it has the features, dependencies, build steps and code layout. This file
adds what you need to change the code safely.

## Project facts

- C++17, Qt 6 Widgets, CMake. One executable target, `OnionImageViewer` (`qt_add_executable`, `WIN32`).
- Windows is the target platform (MSVC, Qt 6.11 `msvc2022_64`). Windows-only code (shell
  thumbnails, clipboard drop effects, shell icons) sits behind `#ifdef Q_OS_WIN`, so the rest
  still compiles with GCC/Clang on Linux, which is handy for headless checks
  (`QT_QPA_PLATFORM=offscreen`).
- Optional features are compile-time switches set by CMake: `ONION_HAVE_OPENEXR` (OpenEXR found)
  and `ONION_HAVE_QTPDF` (Qt PDF found). Code using them must build with and without them.
- New source files must be added to the `qt_add_executable` list in `CMakeLists.txt`.
- The version is `project(OnionImageViewer VERSION x.y.z)` in `CMakeLists.txt`, unless
  `-DONION_VERSION` overrides it (the release workflow passes the GitHub release tag). It reaches
  the code through the generated `Version.h` (`ONION_VERSION_STRING`).
- `.github/workflows/release-installer.yml` builds the installer when a GitHub release is
  published and attaches it to the release. It fetches Qt from download.qt.io itself (aqtinstall
  can't read Qt 6.11's repository layout); add new Qt modules to its package list.

## Building

On Windows, `build-run.bat` from the repository folder does everything (see README). By hand, use
the "x64 Native Tools Command Prompt" and:

```bat
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
cmake --build build-msvc
```

- The app must not be running when you build: it locks `build-msvc\OnionImageViewer.exe`.
- Build folders (`build/`, `build-*/`) are git-ignored. Don't commit binaries, `vcpkg_installed/`
  or `installer/stage/` and `installer/Output/`.
- The build must stay free of warnings at `/W4 /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic`.

There is no automated test suite yet. Check a change by building it and, where you can, by
running the app on real files.

## Code conventions

- Follow the style already in the file: 4-space indent, braces on every `if`, `m_` prefix for
  members, `final` on concrete classes, `#pragma once` in headers.
- `QT_NO_CAST_FROM_ASCII` and `QT_NO_CAST_TO_ASCII` are on: write string literals as
  `QStringLiteral("...")` (or `QLatin1String`), never bare `"..."` where a `QString` is expected.
- Free functions are grouped in namespaces per file (`Dpx::`, `Exr::`, `Psd::`, `Vector::`,
  `ColorTransform::`, `Metadata::`, `FileOps::`, `Icons::`), with helpers in an anonymous
  namespace in the .cpp.
- Comments explain *why* and what a function promises, in plain sentences, at the top of the
  declaration in the header. Keep them up to date when behaviour changes.
- Keep the UI thread free of slow work. Disk reads, decoding and thumbnails run on background
  threads (`FrameLoader`, `QThreadPool`). The user works with network shares (SMB), mapped
  drives and OneDrive "online-only" files:
  - never read a file's contents just to show a thumbnail in a folder listing (it downloads
    OneDrive files); use the Windows shell thumbnail cache;
  - avoid calls that touch every drive root on the UI thread;
  - read files sequentially rather than with many scattered reads.
- Readers keep the stored values (no colour conversion on load) and hand them to
  `ColorTransform::toDisplay()`. Add a new format by writing a reader with `isXxxFile()` and
  `read()` like `src/DpxReader.h`, then hook it in where the other readers are: loading in
  `src/ImageLoader.cpp`, thumbnails and the list of image extensions in `src/MainWindow.cpp`,
  and header metadata in `src/Metadata.cpp`.
- User-visible settings persist through `QSettings` with keys defined as constants near the top
  of `MainWindow.cpp`.
- The folder panel (`FolderTreePanel`) is two views stacked to look like one Explorer-style
  pane: a `QTreeWidget` with the Quick access and This PC headings (pins are the children of
  Quick access) and below it the `QFileSystemModel` drive tree, indented to sit under This PC.
  Pins are stored by `MainWindow` (`folders/pinned`) whenever they change. Icons for pins come
  from the shell only for folders on local fixed disks; never ask the shell for icons of network
  or removable paths on the UI thread.
- Batch files, PowerShell scripts and the Inno Setup script use CRLF line endings
  (`.gitattributes`).

## Pull requests

- Work on a branch and open a PR against `main`. Keep each PR to one change, with a description
  of what changed and how it was checked.
- If you add a dependency, a build option or a new format, update `README.md` (and this file if
  agents need to know about it).
