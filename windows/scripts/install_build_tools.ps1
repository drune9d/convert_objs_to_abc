Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

trap {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

function Write-Section {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message"
}

function Test-Command {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-WingetPackage {
    param(
        [string]$Id,
        [string]$Name,
        [string[]]$ExtraArgs = @()
    )

    Write-Section "Installing $Name"
    $args = @(
        "install",
        "--id", $Id,
        "--exact",
        "--source", "winget",
        "--accept-package-agreements",
        "--accept-source-agreements"
    ) + $ExtraArgs

    & winget @args
    if ($LASTEXITCODE -ne 0) {
        throw "winget could not install $Name."
    }
}

if (-not (Test-Command "winget")) {
    throw "winget was not found. Install App Installer from the Microsoft Store, then run this again."
}

if (-not (Test-Command "git")) {
    Install-WingetPackage -Id "Git.Git" -Name "Git for Windows"
}
else {
    Write-Host "Git is already installed."
}

if (-not (Test-Command "python")) {
    Install-WingetPackage -Id "Python.Python.3.12" -Name "Python 3"
}
else {
    Write-Host "Python is already installed."
}

if (-not (Test-Command "cmake")) {
    Install-WingetPackage -Id "Kitware.CMake" -Name "CMake"
}
else {
    Write-Host "CMake is already installed."
}

Install-WingetPackage `
    -Id "Microsoft.VisualStudio.2022.BuildTools" `
    -Name "Visual Studio 2022 Build Tools C++ workload" `
    -ExtraArgs @(
        "--override",
        "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    )

Write-Host ""
Write-Host "Install complete. Open a new terminal or Explorer window before building."
