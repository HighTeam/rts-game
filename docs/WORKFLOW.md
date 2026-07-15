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

- `main` — always buildable
- `feature/<short-name>` — short-lived branches, merge via PR or fast-forward when solo

Even solo, branches keep history bisectable when a desync bug appears weeks later.

## Issue hierarchy

1. **Epic** — one GitHub Issue per `###` section in [BACKLOG.md](BACKLOG.md); checklist in the issue body
2. **Task** — split any epic item that grows past one sitting into its own issue; link it to the epic

Close epics when all checklist items are done.

## What to work on first

1. All `[BLOCKING]` items in M0
2. M1 deterministic sim + data-driven civ definitions
3. M2 lockstep — do not steal time from this milestone

If behind at week 6, trim M4 tech-tree depth first, not networking or M1 architecture.
