# Build aoa-setup.exe from a Release output folder (NSIS, unsigned).
#
#   cmake --build build\x64-release --config Release --target aoa
#   .\scripts\package-setup.ps1
#
# Requires Nullsoft Install System (makensis). Install: winget install NSIS.NSIS

[CmdletBinding()]
param(
    [string]$SourceDir = "",
    [string]$OutFile = "",
    [string]$RepoRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $RepoRoot "build\x64-release\Release"
}

$gameExe = Join-Path $SourceDir "AgeofAffinities.exe"
$assetsDat = Join-Path $SourceDir "assets.dat"
if (-not (Test-Path -LiteralPath $gameExe)) {
    throw "Missing $gameExe. Build Release first: cmake --build build\x64-release --config Release --target aoa"
}
if (-not (Test-Path -LiteralPath $assetsDat)) {
    throw "Missing $assetsDat. Build Release so POST_BUILD packs assets."
}

if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path $SourceDir "aoa-setup.exe"
}

$makensis = @(
    "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
    "${env:ProgramFiles}\NSIS\makensis.exe",
    (Join-Path $env:LOCALAPPDATA "Programs\NSIS\makensis.exe")
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $makensis) {
    $cmd = Get-Command makensis -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        $makensis = $cmd.Source
    }
}
if (-not $makensis) {
    throw "makensis.exe not found. Install NSIS: winget install NSIS.NSIS"
}

$constantsPath = Join-Path $RepoRoot "src\core\constants.hpp"
$constantsText = Get-Content -LiteralPath $constantsPath -Raw
if ($constantsText -notmatch 'GAME_VERSION = "([^"]+)"') {
    throw "Could not read GAME_VERSION from $constantsPath"
}
$displayVersion = $Matches[1]

$cmakeLists = Get-Content -LiteralPath (Join-Path $RepoRoot "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch 'project\(age-of-affinities\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') {
    throw "Could not read CMake project VERSION"
}
$productVersion = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"

$stage = Join-Path $env:TEMP "aoa-nsis-stage"
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "scenarios") | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "patterns") | Out-Null

Copy-Item -LiteralPath $gameExe -Destination $stage
Copy-Item -LiteralPath $assetsDat -Destination $stage
Get-ChildItem -LiteralPath $SourceDir -Filter "*.dll" | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stage
}

$hasPatternMaker = $false
$patternMaker = Join-Path $SourceDir "PatternMaker.exe"
if (Test-Path -LiteralPath $patternMaker) {
    Copy-Item -LiteralPath $patternMaker -Destination $stage
    $hasPatternMaker = $true
}

$srcScenarios = Join-Path $SourceDir "scenarios"
if (Test-Path -LiteralPath $srcScenarios) {
    Copy-Item -Path (Join-Path $srcScenarios "*") -Destination (Join-Path $stage "scenarios") -Recurse -Force
}

$srcPatterns = Join-Path $SourceDir "patterns"
if (Test-Path -LiteralPath $srcPatterns) {
    Copy-Item -Path (Join-Path $srcPatterns "*") -Destination (Join-Path $stage "patterns") -Recurse -Force
}
if (-not (Get-ChildItem -LiteralPath (Join-Path $stage "patterns") -Force)) {
    New-Item -ItemType File -Path (Join-Path $stage "patterns\.gitkeep") | Out-Null
}

$eulaSrc = Join-Path $RepoRoot "legal\EULA.md"
$eulaDst = Join-Path $stage "EULA.txt"
Copy-Item -LiteralPath $eulaSrc -Destination $eulaDst

$vcRedist = Get-ChildItem -Path @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\vc_redist.x64.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\vc_redist.x64.exe"
) -ErrorAction SilentlyContinue | Select-Object -First 1

$nsi = Join-Path $RepoRoot "packaging\aoa-setup.nsi"

$makensisArgs = @(
    "/V2",
    "/DAOA_STAGE_DIR=$stage",
    "/DAOA_OUT_FILE=$OutFile",
    "/DAOA_LICENSE_FILE=$eulaDst",
    "/DAOA_DISPLAY_VERSION=$displayVersion",
    "/DAOA_PRODUCT_VERSION=$productVersion"
)
if ($hasPatternMaker) {
    $makensisArgs += "/DAOA_HAS_PATTERN_MAKER=1"
}
if ($null -ne $vcRedist) {
    $makensisArgs += "/DAOA_VCREDIST_FILE=$($vcRedist.FullName)"
    Write-Host "Including VC++ redistributable: $($vcRedist.FullName)"
} else {
    Write-Warning "vc_redist.x64.exe not found; setup will not install the Visual C++ runtime."
}
$makensisArgs += $nsi

Write-Host "Staging $SourceDir -> $stage"
Write-Host "Compiling $OutFile ($displayVersion)"
& $makensis @makensisArgs
if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}

Write-Host "Wrote $OutFile"
