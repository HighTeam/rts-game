param(
    [ValidateSet("Debug", "Release", "Both")]
    [string]$Configuration = "Both"
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\bootstrap.ps1"

function Invoke-AoaBuild {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Debug", "Release")]
        [string]$Config
    )

    $Preset = if ($Config -eq "Debug") { "x64-debug" } else { "x64-release" }

    Write-Host "=== Configure $Preset ==="
    cmake --preset $Preset

    Write-Host "=== Build $Preset ==="
    cmake --build --preset $Preset
}

switch ($Configuration) {
    "Debug" { Invoke-AoaBuild -Config Debug }
    "Release" { Invoke-AoaBuild -Config Release }
    "Both" {
        Invoke-AoaBuild -Config Debug
        Invoke-AoaBuild -Config Release
    }
}
