# GitHub setup

## What is configured

| Item | Link |
|------|------|
| Organization | https://github.com/HighTeam |
| Game repo (private) | https://github.com/HighTeam/rts-game |
| Website repo (public) | https://github.com/HighTeam/rts-game-web |
| Project board (Kanban) | https://github.com/orgs/HighTeam/projects/1 |
| Issues (21 epics) | https://github.com/HighTeam/rts-game/issues |
| Milestones M0-M5 | https://github.com/HighTeam/rts-game/milestones |
| Labels | `blocking`, `engine`, `netcode`, `content`, `ui`, `tooling`, `epic` |

The **Age of Affinities** org project is linked to `HighTeam/rts-game`. All 21 epic issues are on the board.

### Board columns (Status field)

| Column | Use |
|--------|-----|
| Backlog | Not started |
| Ready | Queued next |
| In progress | Active work |
| In review | PR / verification |
| Done | Closed |

**Current focus:** [#1 [M0] Build & repo setup](https://github.com/HighTeam/rts-game/issues/1) is **In progress**.

### Daily workflow

1. Pick the top `[BLOCKING]` issue in the current milestone
2. Move it to **In progress** on the board
3. Check off subtasks in the issue body (`- [ ]` → `- [x]`)
4. Close the issue when the checklist is complete

## CLI helpers

Link repo and add all open issues to the org project:

```powershell
.\scripts\link-project.ps1 -Owner HighTeam -ProjectNumber 1
```

Full reset (labels, milestones, issues — skips project creation):

```powershell
.\scripts\setup-github.ps1 -SkipProject
```

## Auth scopes (one-time)

```powershell
gh auth refresh -h github.com -s project,read:project
```

## Migration note

Repo was transferred from `4ord-dev/rts-game` to `HighTeam/rts-game`. The old personal project board can be archived; use the org project linked above.
