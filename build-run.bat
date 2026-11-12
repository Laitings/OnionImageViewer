@echo off
rem Builds OnionImageViewer in build-msvc and starts it (see tools\build-run.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-run.ps1" %*
exit /b %errorlevel%
