param(
    [Parameter(Mandatory = $true)]
    [string]$HostAddress,
    [uint16]$Port = 27000,
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build\x64-release\Release\AgeofAffinities.exe"

if (-not (Test-Path $exe)) {
    Write-Error "Build not found: $exe`nRun: cmake --build build\x64-release --config Release"
}

$joinTarget = "{0}:{1}" -f $HostAddress, $Port
$args = @("--lockstep-join", $joinTarget)
if ($Debug) {
    $args += "--lockstep-debug"
}

Write-Host "Connecting to $joinTarget ..."
& $exe @args
