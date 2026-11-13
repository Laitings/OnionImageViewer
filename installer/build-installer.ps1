# Builds OnionImageViewer in Release, collects everything it needs to run (Qt, the MSVC runtime,
# the ARRI LUTs from resources\luts\arri) into installer\stage and compiles installer\OnionImageViewer.iss
# into installer\Output\OnionImageViewer-Setup-<version>.exe.
#
# Needs: Visual Studio with the C++ workload, Qt (msvc2022_64 with the Image Formats add-on) and
# Inno Setup 6 (winget install JRSoftware.InnoSetup). Run it from any prompt; it sets up the
# Visual Studio x64 environment itself. Close the app first if it is running from the build.
#
#   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
#   ... -QtDir C:\Qt\6.11.0\msvc2022_64 -SkipBuild
#   ... -ArriLutDir Q:\Davinci_Shared_LUTs\Arri   (other ARRI LUTs than the ones in resources\luts\arri)
#   ... -Version 1.2.0     (overrides the version from CMakeLists.txt; the release workflow passes the tag)

param(
    [string]$QtDir = "",
    [string]$ArriLutDir = "",
    [string]$BuildDir = "",
    [string]$Version = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$installerDir = $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build-installer" }
$stageDir = Join-Path $installerDir "stage"
$outputDir = Join-Path $installerDir "Output"

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host ""; Write-Host "ERROR: $text" -ForegroundColor Red; exit 1 }

# --- Qt: the newest C:\Qt\6.*\msvc2022_64 unless given ----------------------------------------
if (-not $QtDir) {
    $candidates = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^6\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "msvc2022_64" } |
        Where-Object { Test-Path (Join-Path $_ "bin\windeployqt.exe") }
    $QtDir = $candidates | Select-Object -First 1
}
if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir "bin\windeployqt.exe"))) {
    Fail "Qt not found. Pass -QtDir, e.g. -QtDir C:\Qt\6.11.1\msvc2022_64"
}
Write-Host "Qt: $QtDir"

# --- Visual Studio x64 environment (like the "x64 Native Tools" prompt) ------------------------
if (-not $env:VCToolsRedistDir) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { Fail "Visual Studio not found (no vswhere.exe)." }
    $vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { Fail "Visual Studio with the C++ workload not found." }
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    Step "Setting up the Visual Studio environment ($vsPath)"
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
    }
    if (-not $env:VCToolsRedistDir) { Fail "vcvars64.bat did not set up the environment." }
}

# --- Inno Setup -------------------------------------------------------------------------------
$iscc = @(
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { Fail "Inno Setup 6 not found. Install it with: winget install JRSoftware.InnoSetup" }

# --- Version: -Version if given, otherwise from CMakeLists.txt ---------------------------------
if ($Version) {
    $version = $Version -replace '^[vV]', ''
} else {
    $cmakeText = Get-Content (Join-Path $repo "CMakeLists.txt") -Raw
    if ($cmakeText -notmatch 'project\(OnionImageViewer\s+VERSION\s+([0-9.]+)') { Fail "No VERSION in CMakeLists.txt" }
    $version = $matches[1]
}
if ($version -notmatch '^[0-9A-Za-z.\-+]+$') { Fail "Version '$version' has characters a file name can't hold." }
Write-Host "Version: $version"

# --- Build --------------------------------------------------------------------------------------
if (-not $SkipBuild) {
    Step "Configuring ($BuildDir)"
    $configure = @("-S", $repo, "-B", $BuildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=$QtDir", "-DONION_VERSION=$version")
    if ($ArriLutDir) {
        if (-not (Test-Path $ArriLutDir)) { Fail "ARRI LUT folder $ArriLutDir not found." }
        $configure += "-DONION_ARRI_LUT_DIR=$($ArriLutDir -replace '\\','/')"
    } else {
        $configure += "-DONION_ARRI_LUT_DIR="
    }
    & cmake @configure
    if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed." }

    Step "Building"
    & cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { Fail "Build failed. Is OnionImageViewer.exe still running from the build folder?" }
}
$exe = Join-Path $BuildDir "OnionImageViewer.exe"
if (-not (Test-Path $exe)) { Fail "$exe not found." }

# --- Stage --------------------------------------------------------------------------------------
Step "Collecting files in $stageDir"
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory $stageDir | Out-Null
Copy-Item $exe $stageDir
# DLLs the build put next to the .exe: libheif and libde265 (LGPL, so not linked statically).
Get-ChildItem $BuildDir -Filter *.dll -File | Copy-Item -Destination $stageDir
$licenses = Join-Path $BuildDir "licenses"
if (Test-Path $licenses) { Copy-Item $licenses (Join-Path $stageDir "licenses") -Recurse }
if (-not (Test-Path (Join-Path $stageDir "heif.dll"))) { Write-Host "Note: no heif.dll in the build, so the installer has no HEIC support." -ForegroundColor Yellow }

& (Join-Path $QtDir "bin\windeployqt.exe") --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    --no-compiler-runtime (Join-Path $stageDir "OnionImageViewer.exe")
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed." }

# The MSVC runtime goes next to the .exe ("app-local"), so no separate VC++ redistributable is needed.
$crt = Get-ChildItem (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
if (-not $crt) { Fail "MSVC runtime not found under $env:VCToolsRedistDir" }
Copy-Item (Join-Path $crt.FullName "*.dll") $stageDir

$luts = Join-Path $BuildDir "luts"
if (Test-Path $luts) {
    Copy-Item $luts (Join-Path $stageDir "luts") -Recurse
    Write-Host "ARRI LUTs: $((Get-ChildItem (Join-Path $stageDir 'luts') -Filter *.cube).Count)"
}

# --- Installer ----------------------------------------------------------------------------------
Step "Compiling the installer"
New-Item -ItemType Directory $outputDir -Force | Out-Null
$numericVersion = if ($version -match '^(\d+(\.\d+){0,3})') { $matches[1] } else { "0.0.0" }
& $iscc /Q "/DAppVersion=$version" "/DNumericVersion=$numericVersion" "/DStageDir=$stageDir" "/DOutputDir=$outputDir" (Join-Path $installerDir "OnionImageViewer.iss")
if ($LASTEXITCODE -ne 0) { Fail "Inno Setup failed." }

$setup = Join-Path $outputDir "OnionImageViewer-Setup-$version.exe"
Write-Host ""
Write-Host "Done: $setup" -ForegroundColor Green
Write-Host ("Size: {0:N1} MB" -f ((Get-Item $setup).Length / 1MB))
