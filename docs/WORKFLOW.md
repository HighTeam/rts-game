# Project workflow

## Board

Use one GitHub Project with a **Kanban** view as the daily board.

| Column | Meaning |
|--------|---------|
| Backlog | Not started |
| In Progress | Active work |
| Blocked | Waiting on a dependency or decision |
| Done | Closed |

Add a **Roadmap** view later if you want a timeline; skip **Feature release** until post-v1.0 patches.

Project link: see [GITHUB_SETUP.md](GITHUB_SETUP.md). Issues: https://github.com/HighTeam/rts-game/issues

## Milestones

| Milestone | Target | Focus |
|-----------|--------|-------|
| M0 — Foundations | Week 1 | CMake, sim/render split, fixed-point, ECS skeleton |
| M1 — Single-civ sim | Weeks 2–3 | Deterministic gameplay, data-driven civ JSON |
| M2 — Lockstep 2p | Weeks 4–5 | ENet, lockstep, reconnect — **protect this** |
| M3 — 8p + save/load | Week 6 | Scale networking, persistence |
| M4 — Civ content ×4 | Weeks 7–9 | Water, Earth, Fire, Air |
| M5 — UI + packaging | Weeks 10–11 | Menus, AI, installer, download page |

Full task lists: [BACKLOG.md](BACKLOG.md).

## Labels

| Label | Use for |
|-------|---------|
| `blocking` | Gates other work |
| `engine` | Sim, ECS, rendering core |
| `netcode` | ENet, lockstep, reconnect |
| `content` | Civs, units, tech trees |
| `ui` | Menus, lobby, settings |
| `tooling` | CI, build, packaging, scripts |
| `epic` | Top-level checklist issue (one per `###` in backlog) |

## Branch strategy

- `main` — always buildable; **no direct commits or merges** (process rule — see below)
- Short-lived branches from `main`, merged only via pull request

### Branch naming

| Prefix | Use for |
|--------|---------|
| `feature/` | New capability (e.g. `feature/m0-build-setup`) |
| `fix/` | Bug fixes (e.g. `fix/lockstep-desync`) |
| `docs/` | Documentation-only |
| `tooling/` | CI, scripts, build system |

Use lowercase and hyphens: `feature/fixed-point-math`, not `feature/FixedPoint`.

### Pull request workflow (required)

GitHub Free on a private org repo **cannot** enforce branch protection rules that block direct pushes to `main`. Treat the following as mandatory for all contributors and agents:

```
feature/ | fix/ | docs/ | tooling/  →  PR  →  CI checks  →  decision
                                                      ├─ Approve → merge → delete branch
                                                      └─ Changes requested → feedback → push fixes
```

1. Create a branch from up-to-date `main`.
2. Commit work on that branch.
3. Open a **pull request** into `main`. Link the related issue (`Closes #1` or `Refs #1`).
4. Wait for **CI** (`.github/workflows/build.yml`) to pass on the PR.
5. **Review decision:**
   - **Approve** → merge the PR (squash or merge commit — prefer merge commit for bisect clarity), then **delete the branch**.
   - **Changes requested** → address feedback, push to the same branch, re-request review.

Do not push directly to `main`, even when working solo. PRs preserve history, run checks, and keep multi-agent / multi-developer work organized.

Even solo, branches keep history bisectable when a desync bug appears weeks later.

## Issue hierarchy

1. **Epic** — one GitHub Issue per `###` section in [BACKLOG.md](BACKLOG.md); checklist in the issue body
2. **Task** — split any epic item that grows past one sitting into its own issue; link it to the epic

Close epics when all checklist items are done.

## What to work on first

1. Lockstep robustness on `main` (open fix drafts **#72** / **#75** / **#77** / **#79** /
   **#81** / **#83** / **#85** / **#87** / **#89**) — see [LOCKSTEP.md](LOCKSTEP.md)
2. Remaining M3 gaps: 8-player CI smoke, MP coordinated save/load — [BACKLOG.md](BACKLOG.md)
3. M4 civ content and M5 packaging polish (without regressing lockstep)

If schedule slips: cut M4 tech-tree depth before cutting lockstep robustness or data-driven architecture.
