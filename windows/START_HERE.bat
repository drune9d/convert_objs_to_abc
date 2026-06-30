@echo off
setlocal
cd /d "%~dp0"
title OBJ Sequence to Alembic

set "APP_EXE=%~dp0dist\OBJ Sequence to Alembic.exe"

if exist "%APP_EXE%" (
  start "" "%APP_EXE%"
  exit /b 0
)

call "%~dp0OBJ_to_ABC_Windows.bat"
