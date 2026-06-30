param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$Triplet = "x64-windows-static",
    [string]$Generator = "",
    [string]$Platform = "",
    [switch]$Clean,
    [switch]$SkipVcpkgBootstrap
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

trap {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"
$BinDir = Join-Path $Root "bin"
$DepsDir = Join-Path $Root "_deps"
$DefaultVcpkgRoot = Join-Path $DepsDir "vcpkg"

function Write-Section {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message"
}

function Require-Command {
    param(
        [string]$Name,
        [string]$InstallHint
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name was not found. $InstallHint"
    }
}

function Find-FirstExistingFile {
    param([string[]]$Paths)

    foreach ($Path in $Paths) {
        if ($Path -and (Test-Path -LiteralPath $Path -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $Path).Path
        }
    }

    return $null
}

function Find-UnderRoots {
    param(
        [string[]]$Roots,
        [string]$Filter
    )

    foreach ($SearchRoot in $Roots) {
        if (-not (Test-Path -LiteralPath $SearchRoot -PathType Container)) {
            continue
        }

        $Match = Get-ChildItem -LiteralPath $SearchRoot -Recurse -Filter $Filter -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($Match) {
            return $Match.FullName
        }
    }

    return $null
}

function Get-CMakeExe {
    if ($env:CMAKE_EXE -and (Test-Path -LiteralPath $env:CMAKE_EXE -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $env:CMAKE_EXE).Path
    }

    $Command = Get-Command "cmake" -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    $Known = Find-FirstExistingFile @(
        "D:\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "D:\Microsoft Visual Studio\17\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "D:\Program Files\CMake\bin\cmake.exe",
        "D:\Program Files (x86)\CMake\bin\cmake.exe",
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe"
    )
    if ($Known) {
        return $Known
    }

    $Found = Find-UnderRoots @(
        "D:\Microsoft Visual Studio",
        "C:\Program Files\Microsoft Visual Studio",
        "C:\Program Files (x86)\Microsoft Visual Studio"
    ) "cmake.exe"
    if ($Found) {
        return $Found
    }

    throw "cmake.exe was not found. Double-click INSTALL_BUILD_TOOLS.bat, or install CMake for Windows and open a new terminal."
}

function Set-DefaultGenerator {
    param([string]$CMakeExe)

    if ($Generator) {
        return
    }

    $Help = & $CMakeExe --help 2>$null
    if ($Help -match "Visual Studio 18 2026") {
        $script:Generator = "Visual Studio 18 2026"
        if (-not $script:Platform) {
            $script:Platform = "x64"
        }
    }
    elseif ($Help -match "Visual Studio 17 2022") {
        $script:Generator = "Visual Studio 17 2022"
        if (-not $script:Platform) {
            $script:Platform = "x64"
        }
    }
}

function Remove-UnderRoot {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    if (-not $resolvedPath.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside this Windows package: $resolvedPath"
    }

    Remove-Item -LiteralPath $resolvedPath -Recurse -Force
}

function Get-VcpkgRoot {
    if ($env:VCPKG_ROOT -and (Test-Path -LiteralPath $env:VCPKG_ROOT)) {
        return (Resolve-Path -LiteralPath $env:VCPKG_ROOT).Path
    }

    return $DefaultVcpkgRoot
}

$CMakeExe = Get-CMakeExe
Set-DefaultGenerator -CMakeExe $CMakeExe

Write-Host "Using CMake: $CMakeExe"
if ($Generator) {
    Write-Host "Using generator: $Generator"
}

if ($Clean) {
    Write-Section "Cleaning Windows build output"
    Remove-UnderRoot $BuildDir
    Remove-UnderRoot $BinDir
}

$VcpkgRoot = Get-VcpkgRoot
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"

if (-not (Test-Path -LiteralPath $VcpkgExe)) {
    if ($SkipVcpkgBootstrap) {
        throw "vcpkg.exe was not found at $VcpkgExe."
    }

    Require-Command "git" "Install Git for Windows, then open a new terminal. Winget: winget install Git.Git"

    Write-Section "Bootstrapping local vcpkg"
    New-Item -ItemType Directory -Force -Path $DepsDir | Out-Null
    if (-not (Test-Path -LiteralPath $VcpkgRoot)) {
        git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
        if ($LASTEXITCODE -ne 0) {
            throw "git clone failed while downloading vcpkg."
        }
    }

    & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg bootstrap failed."
    }
}

$ToolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $ToolchainFile)) {
    throw "vcpkg CMake toolchain file was not found: $ToolchainFile"
}

Write-Section "Configuring"
$ConfigureArgs = @(
    "-S", $Root,
    "-B", $BuildDir,
    "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DCMAKE_BUILD_TYPE=$Config"
)

if ($Generator) {
    $ConfigureArgs += @("-G", $Generator)
}
if ($Platform) {
    $ConfigureArgs += @("-A", $Platform)
}

& $CMakeExe @ConfigureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

Write-Section "Building"
& $CMakeExe --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

$ExePath = Join-Path $BinDir "Objs2Abc.exe"
if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Build finished, but the converter was not found at $ExePath"
}

Write-Host ""
Write-Host "Build complete:"
Write-Host $ExePath
