@echo off
setlocal
cd /d "%~dp0"

where py >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  py -3 "%~dp0gui.py"
  exit /b %ERRORLEVEL%
)

where python >nul 2>nul
if %ERRORLEVEL% EQU 0 (
  python "%~dp0gui.py"
  exit /b %ERRORLEVEL%
)

echo Python 3 was not found.
echo Install Python 3 for Windows from https://www.python.org/downloads/windows/
echo Make sure "Add python.exe to PATH" is enabled, then run this again.
pause
exit /b 1
