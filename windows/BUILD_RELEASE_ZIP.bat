@echo off
setlocal
cd /d "%~dp0"
title Package OBJ Sequence to Alembic

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package_release.ps1" %*
if errorlevel 1 (
  echo.
  echo Packaging failed. See the messages above.
  pause
  exit /b 1
)

echo.
echo Release zip is ready:
echo %~dp0dist\OBJ-Sequence-to-Alembic-Windows.zip
echo.
pause
