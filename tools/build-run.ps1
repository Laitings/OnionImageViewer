# Builds OnionImageViewer (Release, in build-msvc) and starts it. Works from any prompt: it sets
# up the Visual Studio x64 environment and finds Qt by itself. A copy of the viewer that is
# running from build-msvc is closed first, because it locks the .exe.
#
#   build-run.bat                      (from the repository folder)
#   ... -QtDir C:\Qt\6.11.0\msvc2022_64 -NoRun

param(
    [string]$QtDir = "",
    [string]$ArriLutDir = "Q:\Davinci_Shared_LUTs\Arri",
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo "build-msvc"
$exe = Join-Path $buildDir "OnionImageViewer.exe"

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host ""; Write-Host "ERROR: $text" -ForegroundColor Red; exit 1 }

# --- Qt: the newest C:\Qt\6.*\msvc2022_64 unless given ----------------------------------------
if (-not $QtDir) {
    $QtDir = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "msvc2022_64" } |
        Where-Object { Test-Path (Join-Path $_ "bin\windeployqt.exe") } |
        Select-Object -First 1
}
if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir "bin\windeployqt.exe"))) {
    Fail "Qt not found. Pass -QtDir, e.g. -QtDir C:\Qt\6.11.1\msvc2022_64"
}

# --- Visual Studio x64 environment --------------------------------------------------------------
if (-not $env:VCToolsInstallDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { Fail "Visual Studio not found (no vswhere.exe)." }
    $vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { Fail "Visual Studio with the C++ workload not found." }
    cmd /c "`"$(Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
    }
    if (-not $env:VCToolsInstallDir) { Fail "Could not set up the Visual Studio environment." }
}

# --- Close a running copy (it locks the .exe) ----------------------------------------------------
$running = Get-Process OnionImageViewer -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and ([IO.Path]::GetFullPath($_.Path) -eq [IO.Path]::GetFullPath($exe))
}
if ($running) {
    Step "Closing the running viewer"
    $running | ForEach-Object { [void]$_.CloseMainWindow() }
    $running | ForEach-Object { if (-not $_.WaitForExit(5000)) { Stop-Process -Id $_.Id -Force } }
}

# --- Configure (first time only; later builds re-run it by themselves when needed) --------------
# A build folder from before HEIF support (vcpkg triplet x64-windows-static-md) is configured
# afresh once, so vcpkg builds libheif as a DLL and OpenEXR is found in the new triplet.
$cache = Join-Path $buildDir "CMakeCache.txt"
$fresh = (Test-Path $cache) -and (Select-String -Path $cache -Pattern "VCPKG_TARGET_TRIPLET:STRING=x64-windows-static-md" -SimpleMatch -Quiet)
if ($fresh -or -not (Test-Path $cache)) {
    Step "Configuring build-msvc (Qt: $QtDir)"
    $configure = @("-S", $repo, "-B", $buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=$QtDir")
    if ($fresh) { $configure += "--fresh" }
    if ($ArriLutDir -and (Test-Path $ArriLutDir)) {
        $configure += "-DONION_ARRI_LUT_DIR=$($ArriLutDir -replace '\\','/')"
    }
    & cmake @configure
    if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed." }
}

Step "Building"
& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { Fail "Build failed." }

Step "Copying Qt next to the .exe"
& (Join-Path $QtDir "bin\windeployqt.exe") --release --no-translations --verbose 0 $exe
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed." }

if (-not $NoRun) {
    Step "Starting OnionImageViewer"
    Start-Process $exe -WorkingDirectory $buildDir
}
