# GitHub setup

## What is already configured

| Item | Link |
|------|------|
| Repository | https://github.com/4ord-dev/rts-game |
| Issues (21 epics) | https://github.com/4ord-dev/rts-game/issues |
| Milestones M0-M5 | https://github.com/4ord-dev/rts-game/milestones |
| Labels | `blocking`, `engine`, `netcode`, `content`, `ui`, `tooling`, `epic` |

## Create the Kanban Project (one-time)

The `gh` CLI needs extra scopes to create Projects via API. Run this once in a terminal:

```powershell
gh auth refresh -h github.com -s project,read:project
```

Then either:

**Option A — CLI**

```powershell
gh project create --owner 4ord-dev --title "Avatar RTS MVP"
```

**Option B — Browser**

1. Open https://github.com/users/4ord-dev/projects
2. **New project** → choose **Kanban** template
3. Name it **Avatar RTS MVP**

### Wire the project to this repo

1. Open the project → **⋯** (top right) → **Settings**
2. Under **Linked repositories**, add `4ord-dev/rts-game`
3. Set the default view to **Board** (Kanban)

### Recommended columns

Rename or add status options:

| Column | Maps to |
|--------|---------|
| Backlog | Open issues, not started |
| In Progress | Active work |
| Blocked | Waiting on dependency |
| Done | Closed issues |

GitHub Projects v2 uses a **Status** field. Edit field options: Project → **⋯** → **Settings** → **Fields** → **Status**.

### Daily workflow

1. Pick the top `[BLOCKING]` issue in the current milestone
2. Move it to **In Progress** on the board
3. Check off subtasks in the issue body (`- [ ]` → `- [x]`)
4. Close the issue when the checklist is complete

## Re-run full setup (new clone / reset)

If you need to recreate labels, milestones, and issues on a fresh repo:

```powershell
.\scripts\setup-github.ps1 -SkipProject
```

Issue bodies live in `scripts/issue-bodies/`.

## Repo description

```powershell
gh repo edit --description "Avatar-themed AoE2-style RTS (C++/OpenGL/ENet lockstep)"
```
