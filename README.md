# Age of Affinities

AoE2-style RTS where civilizations rise through their affinity to elemental powers — starting with Water, Earth, Fire, and Air.

**Organization:** [HighTeam](https://github.com/HighTeam)

## Stack

| Layer | Choice |
|-------|--------|
| Language | C++20+, Windows 10/11 |
| Build | CMake + vcpkg |
| Rendering | OpenGL 3.3 — DE-style hybrid (3D scene, Classic isometric camera); SFML window/menus |
| Simulation | EnTT ECS, fixed-point math, 20 ticks/sec |
| Multiplayer | ENet, deterministic lockstep (up to 8 players) |
| Data | JSON-driven civ/unit/building definitions |

## Links

- **Repo:** https://github.com/HighTeam/rts-game
- **Website:** https://github.com/HighTeam/rts-game-web
- **Issues:** https://github.com/HighTeam/rts-game/issues
- **Milestones:** https://github.com/HighTeam/rts-game/milestones
- **Project board:** https://github.com/orgs/HighTeam/projects/1
- **Backlog:** [docs/BACKLOG.md](docs/BACKLOG.md)
- **Org layout:** [docs/ORG.md](docs/ORG.md)

## Milestones (v1.0 / MVP)

| # | Name | Goal |
|---|------|------|
| M0 | Foundations | Window, render pipeline, sim/render split |
| M1 | Single-civ sim | Deterministic gameplay, one civ, test harness |
| M2 | Lockstep 2p | Two synced clients — highest-risk milestone |
| M3 | 8p + save/load | Scale + persistence |
| M4 | Civ content ×4 | Water, Earth, Fire, Air |
| M5 | UI + packaging | Menus, AI, installer, download page |

## Docs

- [BUILD.md](docs/BUILD.md) — x64 Debug/Release, vcpkg, CMake presets
- [ECS.md](docs/ECS.md) — EnTT components, systems, data-driven hooks
- [DECISIONS.md](docs/DECISIONS.md) — early decision tracker (issue #21)
- [BACKLOG.md](docs/BACKLOG.md) — full epic checklists (M0–M5)
- [WORKFLOW.md](docs/WORKFLOW.md) — Kanban, labels, branches, **PR workflow (required)**
- [ORG.md](docs/ORG.md) — repos, licensing, distribution
- [GITHUB_SETUP.md](docs/GITHUB_SETUP.md) — project board setup

## Status

M1 sim core done (#4, #5). Headless harness (#6) implemented locally. Next: Classic render (#26). Prototype uses top-down debug quads until #26 lands.
