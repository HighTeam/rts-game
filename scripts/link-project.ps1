# Link existing GitHub Project to repo issues
# Usage: .\scripts\link-project.ps1

param(
    [string]$Owner = "HighTeam",
    [string]$Repo = "rts-game",
    [int]$ProjectNumber = 1
)

$ErrorActionPreference = "Stop"
$repoFull = "$Owner/$Repo"

Write-Host "Linking $repoFull to project $ProjectNumber..."
gh project link $ProjectNumber --owner $Owner --repo $repoFull

$issues = gh issue list --repo $repoFull --state open --limit 100 --json number --jq ".[].number"
foreach ($num in $issues) {
    gh project item-add $ProjectNumber --owner $Owner --url "https://github.com/$repoFull/issues/$num" 2>$null
    Write-Host "  issue #$num"
}

Write-Host ""
Write-Host "Project: https://github.com/users/$Owner/projects/$ProjectNumber"
