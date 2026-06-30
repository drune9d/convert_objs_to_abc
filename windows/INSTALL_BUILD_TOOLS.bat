@echo off
setlocal
cd /d "%~dp0"
title Install OBJ Sequence to Alembic Build Tools

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install_build_tools.ps1" %*
if errorlevel 1 (
  echo.
  echo Install failed. See the messages above.
  pause
  exit /b 1
)

echo.
echo Build tools install finished.
echo Close and reopen this folder, then double-click BUILD_STANDALONE_EXE.bat.
echo.
pause
