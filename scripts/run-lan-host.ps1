param(
    [uint16]$Port = 27000,
    [switch]$Debug
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build\x64-release\Release\aoa.exe"

if (-not (Test-Path $exe)) {
    Write-Error "Build not found: $exe`nRun: cmake --build build\x64-release --config Release"
}

$args = @("--lockstep-host", "--port", $Port)
if ($Debug) {
    $args += "--lockstep-debug"
}

Write-Host "Starting host on port $Port ..."
& $exe @args
