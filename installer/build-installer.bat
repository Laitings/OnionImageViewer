@echo off
rem Double-click to build the Windows installer (see build-installer.ps1 for details and options).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-installer.ps1" %*
if errorlevel 1 (
    echo.
    echo Building the installer failed.
)
pause
