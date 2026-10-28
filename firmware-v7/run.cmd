@echo off
cd /d "%~dp0"
if not defined CABADGE_BUILD_ROOT set "CABADGE_BUILD_ROOT=F:\CABadgeBuild"
set "TEMP=%CABADGE_BUILD_ROOT%\temp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
if not exist "%CABADGE_BUILD_ROOT%\venv\Scripts\pythonw.exe" (
  echo Run tools\bootstrap.ps1 from the repository root first.
  pause
  exit /b 1
)
start "" "%CABADGE_BUILD_ROOT%\venv\Scripts\pythonw.exe" -B "workbench.py"
