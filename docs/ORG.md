# HighTeam organization

GitHub org: **HighTeam** — https://github.com/HighTeam

## Repositories

| Repo | Visibility | Role |
|------|------------|------|
| [rts-game](https://github.com/HighTeam/rts-game) | Private | C++ game source, CMake, CI builds |
| [rts-game-web](https://github.com/HighTeam/rts-game-web) | Public | Static website, GitHub Pages, **public** release binaries |

## Project board

Kanban: https://github.com/orgs/HighTeam/projects/1

Tracks `rts-game` epic issues (M0–M5). Website work is tracked under M5 Packaging / separate web repo issues as needed.

## Distribution model

```
rts-game (private)          CI builds installer
        │
        ▼
rts-game-web Releases       public .exe / installer (anyone can download)
        │
        ▼
rts-game-web Pages          marketing + download page links to Releases
```

Source stays private. Players download only the compiled client under the EULA.

## Licensing

| Artifact | License |
|----------|---------|
| `rts-game` source | All Rights Reserved (private) |
| `rts-game-web` source | All Rights Reserved (public repo, proprietary LICENSE) |
| Game installer / .exe | EULA — free to play, no source rights |

Draft EULA: `rts-game-web/content/eula.md` and `legal/EULA.md` in this repo.

## Local clones

```powershell
git clone https://github.com/HighTeam/rts-game.git
git clone https://github.com/HighTeam/rts-game-web.git
```

Default remote for this workspace should point at `HighTeam/rts-game`.
