# 4-process headless stress test: 500 ticks, random input per player, debug logs for all slots.
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $repoRoot "build/x64-release/Release/aoa.exe"
$port = 27010

if (-not (Test-Path $exe)) {
    Write-Error "Build first: cmake --build build/x64-release --config Release"
}

Write-Host "Starting 4-player stress host on port $port..."
$hostProc = Start-Process -FilePath $exe -ArgumentList @(
    "--lockstep-host",
    "--port", "$port",
    "--players", "4",
    "--headless",
    "--ticks", "500",
    "--lockstep-debug",
    "--lockstep-auto-input"
) -PassThru -WindowStyle Normal

Start-Sleep -Seconds 3

$joinArgs = @(
    @{ Slot = 2; Proc = $null },
    @{ Slot = 3; Proc = $null },
    @{ Slot = 4; Proc = $null }
)

foreach ($join in $joinArgs) {
    Write-Host "Starting join player $($join.Slot)..."
    $join.Proc = Start-Process -FilePath $exe -ArgumentList @(
        "--lockstep-join", "127.0.0.1:$port",
        "--players", "4",
        "--player-slot", "$($join.Slot)",
        "--headless",
        "--ticks", "500",
        "--lockstep-debug",
        "--lockstep-auto-input"
    ) -PassThru -WindowStyle Normal
    Start-Sleep -Seconds 2
}

Wait-Process -Id $hostProc.Id -Timeout 900
foreach ($join in $joinArgs) {
    Wait-Process -Id $join.Proc.Id -Timeout 30 -ErrorAction SilentlyContinue
}

Write-Host "Exit codes: host=$($hostProc.ExitCode) p2=$($joinArgs[0].Proc.ExitCode) p3=$($joinArgs[1].Proc.ExitCode) p4=$($joinArgs[2].Proc.ExitCode)"
Write-Host "Logs: $(Join-Path (Split-Path $exe) 'logs')"

if ($hostProc.ExitCode -ne 0 -or $joinArgs[0].Proc.ExitCode -ne 0 -or $joinArgs[1].Proc.ExitCode -ne 0 -or $joinArgs[2].Proc.ExitCode -ne 0) {
    exit 1
}
