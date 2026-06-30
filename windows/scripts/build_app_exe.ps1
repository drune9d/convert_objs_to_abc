param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$Triplet = "x64-windows-static",
    [switch]$SkipNativeBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

trap {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$Root = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$RootPath = $Root.Path
$DepsDir = Join-Path $RootPath "_deps"
$VenvDir = Join-Path $DepsDir "pyinstaller-venv"
$DistDir = Join-Path $RootPath "dist"
$WorkDir = Join-Path $RootPath "build\pyinstaller"
$SpecDir = Join-Path $RootPath "build\pyinstaller-spec"
$AppName = "OBJ Sequence to Alembic"
$AppExe = Join-Path $DistDir "$AppName.exe"
$ConverterExe = Join-Path $RootPath "bin\Objs2Abc.exe"

function Write-Section {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message"
}

function Get-PythonCommand {
    $py = Get-Command "py" -ErrorAction SilentlyContinue
    if ($py) {
        return @($py.Source, "-3")
    }

    $python = Get-Command "python" -ErrorAction SilentlyContinue
    if ($python) {
        return @($python.Source)
    }

    throw "Python 3 was not found. Install Python 3 for Windows, then open a new terminal."
}

function Invoke-Python {
    param([string[]]$Arguments)

    $command = Get-PythonCommand
    $exe = $command[0]
    $prefix = @()
    if ($command.Count -gt 1) {
        $prefix = $command[1..($command.Count - 1)]
    }

    & $exe @prefix @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Python command failed: $($Arguments -join ' ')"
    }
}

function Remove-UnderRoot {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (-not $resolvedPath.StartsWith($RootPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside the Windows package: $resolvedPath"
    }

    Remove-Item -LiteralPath $resolvedPath -Recurse -Force
}

if (-not $SkipNativeBuild) {
    Write-Section "Building standalone native converter"
    & (Join-Path $RootPath "build.ps1") -Config $Config -Triplet $Triplet
    if ($LASTEXITCODE -ne 0) {
        throw "Native converter build failed."
    }
}

if (-not (Test-Path -LiteralPath $ConverterExe)) {
    throw "The converter exe is missing: $ConverterExe"
}

Write-Section "Preparing PyInstaller"
New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $VenvDir "Scripts\python.exe"))) {
    Invoke-Python @("-m", "venv", $VenvDir)
}

$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
& $VenvPython -m pip install --upgrade pip pyinstaller
if ($LASTEXITCODE -ne 0) {
    throw "Could not install PyInstaller into the local virtual environment."
}

Write-Section "Creating one-file Windows app"
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Remove-UnderRoot $WorkDir
Remove-UnderRoot $SpecDir
if (Test-Path -LiteralPath $AppExe) {
    Remove-Item -LiteralPath $AppExe -Force
}

$addDataConverter = "$ConverterExe;bin"
$addDataSample = "$(Join-Path $RootPath "head-poses");head-poses"

& $VenvPython -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --windowed `
    --name $AppName `
    --distpath $DistDir `
    --workpath $WorkDir `
    --specpath $SpecDir `
    --add-data $addDataConverter `
    --add-data $addDataSample `
    (Join-Path $RootPath "gui.py")

if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed."
}

if (-not (Test-Path -LiteralPath $AppExe)) {
    throw "PyInstaller finished, but the app exe was not found: $AppExe"
}

Write-Host ""
Write-Host "Standalone app ready:"
Write-Host $AppExe
