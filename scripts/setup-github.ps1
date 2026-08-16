# GitHub setup script for Age of Affinities
# Run from repo root after: gh auth refresh -h github.com -s project,read:project
#
# Usage:
#   .\scripts\setup-github.ps1              # labels + milestones + issues + project
#   .\scripts\setup-github.ps1 -SkipProject # if project scope not granted yet
#
# Prefer docs/BACKLOG.md as source of truth. This script is for greenfield /
# recovery; day-to-day work updates backlog + existing issues.

param(
    [string]$Owner = "HighTeam",
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
    param([string]$Title, [string]$Description)
    gh api "repos/$repoFull/milestones" -f title="$Title" -f description="$Description" | Out-Null
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

Write-Host "Creating pillar milestones (named pillars from docs/BACKLOG.md)..."
$milestones = @(
    @{ Title = "Polish & Debt"; Description = "Stabilize Earth loop before map/age pivots" },
    @{ Title = "Map Redesign"; Description = "Tiles, layering, nature — before Ages/Civs" },
    @{ Title = "RMG & Fairness"; Description = "Pattern/script RMG + balance validation" },
    @{ Title = "Versioning & Lobby Compatibility"; Description = "Exact version match for multiplayer lobbies" },
    @{ Title = "Ages & Technologies"; Description = "Four ages + data-driven tech hooks" },
    @{ Title = "Civilizations & Generals"; Description = "Four civs + AoM-style generals" },
    @{ Title = "Art & Presentation"; Description = "Shaders, models, biome art" },
    @{ Title = "GUI Redesign"; Description = "HUD/menus from paper reference" },
    @{ Title = "Shell & Packaging"; Description = "Menus, lobby, AI, installer" }
)
foreach ($m in $milestones) { New-Milestone $m.Title $m.Description }

Write-Host "Creating epic issues from scripts/issue-bodies/ (pillar set)..."
$issueDir = Join-Path $PSScriptRoot "issue-bodies"
$epics = @(
    @{ Title = "[Polish] Open debt (MP save, collision, combat path, art tune)"; Milestone = "Polish & Debt"; Labels = @("engine","netcode","epic"); Body = "polish-debt.md" },
    @{ Title = "[Map] Redesign tiles, layering, nature"; Milestone = "Map Redesign"; Labels = @("engine","content","blocking","epic"); Body = "map-redesign.md" },
    @{ Title = "[RMG] Pattern/script generation + fairness"; Milestone = "RMG & Fairness"; Labels = @("engine","content","epic"); Body = "rmg-fairness.md" },
    @{ Title = "[Versioning] Lobby exact-version gate"; Milestone = "Versioning & Lobby Compatibility"; Labels = @("netcode","ui","blocking","epic"); Body = "versioning-lobby.md" },
    @{ Title = "[Ages] Four ages + technologies"; Milestone = "Ages & Technologies"; Labels = @("content","epic"); Body = "ages-tech.md" },
    @{ Title = "[Civs] Four civs + generals (AoM-style)"; Milestone = "Civilizations & Generals"; Labels = @("content","epic"); Body = "civs-generals.md" },
    @{ Title = "[Art] Shaders, models, biome presentation"; Milestone = "Art & Presentation"; Labels = @("engine","content","epic"); Body = "art-presentation.md" },
    @{ Title = "[GUI] Redesign from paper reference"; Milestone = "GUI Redesign"; Labels = @("ui","epic"); Body = "gui-redesign.md" },
    @{ Title = "[Shell] Menus, lobby, AI, packaging"; Milestone = "Shell & Packaging"; Labels = @("ui","tooling","epic"); Body = "shell-packaging.md" }
)
foreach ($e in $epics) {
    $bodyFile = Join-Path $issueDir $e.Body
    Write-Host "  issue: $($e.Title)"
    New-EpicIssue -Title $e.Title -Milestone $e.Milestone -Labels $e.Labels -BodyFile $bodyFile
}

if (-not $SkipProject) {
    Write-Host "Linking issues to existing project (default: project 1)..."
    & (Join-Path $PSScriptRoot "link-project.ps1") -Owner $Owner -Repo $Repo
}

Write-Host ""
Write-Host "Done. Backlog: docs/BACKLOG.md"
Write-Host "Issues: https://github.com/$repoFull/issues"
Write-Host "Project: https://github.com/users/$Owner/projects/1"
