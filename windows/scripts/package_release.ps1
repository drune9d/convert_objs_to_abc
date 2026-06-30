param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [string]$Triplet = "x64-windows-static"
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
$PackageName = "OBJ-Sequence-to-Alembic-Windows"
$DistDir = Join-Path $RootPath "dist"
$StageDir = Join-Path $DistDir $PackageName
$ZipPath = Join-Path $DistDir "$PackageName.zip"
$AppExe = Join-Path $DistDir "OBJ Sequence to Alembic.exe"

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

& (Join-Path $RootPath "scripts\build_app_exe.ps1") -Config $Config -Triplet $Triplet
if ($LASTEXITCODE -ne 0) {
    throw "Standalone app build failed."
}

if (-not (Test-Path -LiteralPath $AppExe)) {
    throw "Standalone app was not found: $AppExe"
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Remove-UnderRoot $StageDir
if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}

New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

Copy-Item -LiteralPath $AppExe -Destination $StageDir -Force
Copy-Item -LiteralPath (Join-Path $RootPath "README.md") -Destination $StageDir -Force
Copy-Item -LiteralPath (Join-Path $RootPath "LICENSE") -Destination $StageDir -Force

Compress-Archive -Path $StageDir -DestinationPath $ZipPath -Force

Write-Host ""
Write-Host "Release package ready:"
Write-Host $ZipPath
