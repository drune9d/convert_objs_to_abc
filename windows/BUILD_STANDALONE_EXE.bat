@echo off
setlocal
cd /d "%~dp0"
title Build OBJ Sequence to Alembic EXE

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build_app_exe.ps1" %*
if errorlevel 1 (
  echo.
  echo Build failed. See the messages above.
  pause
  exit /b 1
)

echo.
echo Standalone app is ready:
echo %~dp0dist\OBJ Sequence to Alembic.exe
echo.
pause
