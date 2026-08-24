# Shipped assets (`assets/`)

Committed runtime sources for Age of Affinities. At build time, `aoa_pack_assets`
packs this tree plus `data/` into **`assets.dat`** next to the executable
(`CMakeLists.txt` POST_BUILD). Release output then drops loose `assets/` / `data/`
copies so portable folders ship `assets.dat` only (plus `scenarios/` and `patterns/`).

## Layout

| Path | Content |
|------|---------|
| `music/` | Background music (interim: copies from `raw-assets/music/`) |
| `sfx/` | Sound effects (interim copies) |
| `textures/` | Sprites / tile art (interim copies) |
| `models/` | 3D models (interim copies) |
| `visuals.json` | HUD / sprite atlas metadata |

Packed at build: `assets/` + `data/` → `assets.dat` (`ASSET_PACK_FILENAME`).

## Source of truth (dev only)

Authoring happens under **`raw-assets/`** (gitignored). Do not reference `raw-assets/`
from the game binary.

## Runtime load order

1. Prefer `assets.dat` next to the exe (portable / installer / Release).
2. Optional loose `assets/` + `data/` fallback for local Debug via `AOA_RUNTIME_ROOT`
   (`src/core/asset_store.*`, `runtime_paths.cpp`).

There is no `AOA_DATA_DIR` env var.

## Still open (M5)

Richer pack tooling (CRUD, audio-specific packs such as `.adp`) and shipping builds
that never fall back to loose trees.

See [docs/DECISIONS.md](../docs/DECISIONS.md), [docs/BUILD.md](../docs/BUILD.md),
[docs/LOCKSTEP.md](../docs/LOCKSTEP.md).
