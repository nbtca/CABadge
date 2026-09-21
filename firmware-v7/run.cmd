@echo off
cd /d "%~dp0"
set "TEMP=F:\CABadgeBuild\temp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
start "" pythonw.exe -B "workbench.py"
