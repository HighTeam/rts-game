# Bootstraps a local vcpkg clone for manifest-mode builds.
# Sets VCPKG_ROOT for the current PowerShell session when sourced with dot-source,
# or when invoked directly before cmake.

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path $PSScriptRoot -Parent
$VcpkgDir = Join-Path $RepoRoot ".tools\vcpkg"
$VcpkgExe = Join-Path $VcpkgDir "vcpkg.exe"
$PinnedCommit = "bf04c909169fdbb30821c02c6eb01f1cd1295d05"

if (-not (Test-Path $VcpkgExe)) {
    Write-Host "Cloning vcpkg into $VcpkgDir ..."
    git clone https://github.com/microsoft/vcpkg.git $VcpkgDir
    Push-Location $VcpkgDir
    try {
        git checkout $PinnedCommit
        & "$VcpkgDir\bootstrap-vcpkg.bat" -disableMetrics
    }
    finally {
        Pop-Location
    }
}

$env:VCPKG_ROOT = $VcpkgDir
Write-Host "VCPKG_ROOT=$env:VCPKG_ROOT"
