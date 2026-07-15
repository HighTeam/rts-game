# GitHub setup script for Avatar RTS
# Run from repo root after: gh auth refresh -h github.com -s project,read:project
#
# Usage:
#   .\scripts\setup-github.ps1              # labels + milestones + issues + project
#   .\scripts\setup-github.ps1 -SkipProject # if project scope not granted yet

param(
    [string]$Owner = "4ord-dev",
    [string]$Repo = "rts-game",
    [switch]$SkipProject
)

$ErrorActionPreference = "Stop"
$repoFull = "$Owner/$Repo"

function New-Label {
    param([string]$Name, [string]$Color, [string]$Description)
    gh label create $Name --repo $repoFull --color $Color --description $Description --force 2>$null
    Write-Host "  label: $Name"
}

function New-Milestone {
    param([string]$Title, [string]$Description, [string]$DueOn)
    $body = @{ title = $Title; description = $Description; due_on = $DueOn } | ConvertTo-Json
    gh api "repos/$repoFull/milestones" --method POST --input - <<< $body | Out-Null
    Write-Host "  milestone: $Title"
}

function New-EpicIssue {
    param(
        [string]$Title,
        [string]$Milestone,
        [string[]]$Labels,
        [string]$BodyFile
    )
    $args = @(
        "issue", "create",
        "--repo", $repoFull,
        "--title", $Title,
        "--milestone", $Milestone,
        "--body-file", $BodyFile
    )
    foreach ($label in $Labels) {
        $args += @("--label", $label)
    }
    gh @args
}

Write-Host "Creating labels..."
$labels = @(
    @{ Name = "blocking"; Color = "B60205"; Description = "Blocks downstream work" },
    @{ Name = "engine"; Color = "0E8A16"; Description = "Sim, ECS, rendering" },
    @{ Name = "netcode"; Color = "1D76DB"; Description = "ENet, lockstep, reconnect" },
    @{ Name = "content"; Color = "FBCA04"; Description = "Civs, units, tech trees" },
    @{ Name = "ui"; Color = "D876E3"; Description = "Menus, lobby, settings" },
    @{ Name = "tooling"; Color = "C5DEF5"; Description = "CI, build, packaging" },
    @{ Name = "epic"; Color = "5319E7"; Description = "Epic with checklist" }
)
foreach ($l in $labels) { New-Label $l.Name $l.Color $l.Description }

Write-Host "Creating milestones..."
# Due dates aligned to 10-week plan from Jul 2026
$milestones = @(
    @{ Title = "M0 - Foundations"; Description = "Window, sim/render split, fixed-point, ECS skeleton"; Due = "2026-07-22T23:59:59Z" },
    @{ Title = "M1 - Single-civ core simulation"; Description = "One civ, deterministic sim, data-driven JSON"; Due = "2026-08-05T23:59:59Z" },
    @{ Title = "M2 - Lockstep networking, 2 players"; Description = "ENet, lockstep sync, reconnect - highest risk"; Due = "2026-08-19T23:59:59Z" },
    @{ Title = "M3 - Scale to 8 players + save/load"; Description = "8-player scale, persistence"; Due = "2026-08-26T23:59:59Z" },
    @{ Title = "M4 - Civilization content x4"; Description = "Water, Earth, Fire, Air content"; Due = "2026-09-16T23:59:59Z" },
    @{ Title = "M5 - Menus, UI, packaging"; Description = "Menus, AI, installer, download page"; Due = "2026-09-30T23:59:59Z" }
)
foreach ($m in $milestones) { New-Milestone $m.Title $m.Description $m.Due }

Write-Host "Creating epic issues from scripts/issue-bodies/..."
$issueDir = Join-Path $PSScriptRoot "issue-bodies"
if (-not (Test-Path $issueDir)) {
    Write-Error "Missing $issueDir — run from a complete repo checkout."
}

Get-ChildItem $issueDir -Filter "*.json" | ForEach-Object {
    $meta = Get-Content $_.FullName | ConvertFrom-Json
    $bodyFile = Join-Path $issueDir $meta.bodyFile
    Write-Host "  issue: $($meta.title)"
    New-EpicIssue -Title $meta.title -Milestone $meta.milestone -Labels $meta.labels -BodyFile $bodyFile
}

if (-not $SkipProject) {
    Write-Host "Linking issues to existing project (default: project 1)..."
    & (Join-Path $PSScriptRoot "link-project.ps1") -Owner $Owner -Repo $Repo
}

Write-Host ""
Write-Host "Done. Issues: https://github.com/$repoFull/issues"
