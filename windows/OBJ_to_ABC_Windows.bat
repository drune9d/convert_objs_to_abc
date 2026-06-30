@echo off
setlocal
cd /d "%~dp0"

if not exist "%~dp0bin\Objs2Abc.exe" (
  echo Objs2Abc.exe is not built yet. Building now...
  call "%~dp0build.bat"
  if errorlevel 1 (
    echo.
    echo Build failed. See the messages above.
    pause
    exit /b 1
  )
)

call "%~dp0launch_gui.bat"
