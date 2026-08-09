# Launch 4 graphical lockstep clients (host + 3 joins) in separate terminals.
# Connect order: P2 -> P3 -> P4. Each process logs with --lockstep-debug and injects random input.
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build/x64-release/Release/aoa.exe"
$port = 27000

if (-not (Test-Path $exe)) {
    Write-Error "Build first: cmake --build build/x64-release --config Release"
}

Write-Host "Opening host (player 1)..."
Start-Process powershell -ArgumentList @(
    "-NoExit",
    "-Command",
    "& '$exe' --lockstep-host --port $port --players 4 --lockstep-debug --lockstep-auto-input"
)

Start-Sleep -Seconds 2

foreach ($slot in 2..4) {
    Write-Host "Opening join player $slot..."
    Start-Process powershell -ArgumentList @(
        "-NoExit",
        "-Command",
        "& '$exe' --lockstep-join 127.0.0.1:$port --players 4 --player-slot $slot --lockstep-debug --lockstep-auto-input"
    )
    Start-Sleep -Seconds 1
}

Write-Host "Started 4 windows. Join in order P2 -> P3 -> P4. Logs under build/x64-release/Release/logs/"
