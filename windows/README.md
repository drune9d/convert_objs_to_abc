# OBJ Sequence to Alembic for Windows

This folder is the Windows version of the OBJ sequence to Alembic converter.
The macOS version lives in [`../macos`](../macos), and the project overview is in
the [repository root](../).

## Easiest Start

If you received a release zip, double-click:

```text
OBJ Sequence to Alembic.exe
```

That standalone app includes the GUI, the converter, and the sample OBJ sequence.
End users do not need Python, CMake, Visual Studio, vcpkg, or Git.

If you are running from this source folder, double-click:

```text
START_HERE.bat
```

If `dist\OBJ Sequence to Alembic.exe` exists, it opens that app. Otherwise it
uses the local Python GUI and builds `bin\Objs2Abc.exe` on first run.

To create the standalone app from source, double-click:

```text
BUILD_STANDALONE_EXE.bat
```

If the build says CMake or Visual Studio Build Tools are missing, double-click:

```text
INSTALL_BUILD_TOOLS.bat
```

To create the release zip, double-click:

```text
BUILD_RELEASE_ZIP.bat
```

## Source Build Requirements

Only the source-folder path needs these tools:

- Visual Studio 2022 Build Tools with the "Desktop development with C++" workload
- CMake for Windows
- Git for Windows
- Python 3 for Windows

The build script creates a local vcpkg checkout at `_deps\vcpkg` if vcpkg is not
already available through `VCPKG_ROOT`. vcpkg then installs Alembic, HDF5, Imath,
and zlib from `vcpkg.json`. The default triplet is `x64-windows-static` so the
native converter is easier to bundle.

## Manual Build

From PowerShell:

```powershell
cd windows
.\build.ps1
```

Useful options:

```powershell
.\build.ps1 -Config Debug
.\build.ps1 -Triplet x64-windows-static
.\build.ps1 -Clean
```

If CMake cannot find a Visual Studio generator automatically, run the script
from "x64 Native Tools Command Prompt for VS 2022", or pass a generator:

```powershell
.\build.ps1 -Generator "Visual Studio 17 2022" -Platform x64
```

## Run the Source GUI

After building:

```powershell
.\launch_gui.bat
```

The GUI can also rebuild the converter with the `Build Converter` button.

## Command Line

```powershell
.\bin\Objs2Abc.exe -i .\head-poses -o .\output\head-poses.abc -f 24 -n Head
```

## Package a Zip

```powershell
.\scripts\package_release.ps1
```

This builds the standalone Windows app and writes a release zip to:

```text
windows\dist\OBJ-Sequence-to-Alembic-Windows.zip
```

The standalone app itself is:

```text
windows\dist\OBJ Sequence to Alembic.exe
```

## Build Only the App Exe

```powershell
.\scripts\build_app_exe.ps1
```

## Notes

- Input should be a folder of `.obj` frame files that sort in frame order.
- The converter writes animated vertex positions, per-frame topology, and UVs
  when the OBJ sequence includes valid face-varying UVs.
- Normals, materials, textures, vertex colors, and multiple independent meshes
  per OBJ are not exported.
