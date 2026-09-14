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

Packed at build: `aoa_pack_assets <assets_dir> <data_dir> <out/assets.dat>`
(`ASSET_PACK_FILENAME`). Pack header magic `AOA1` (`0x31414F41`) / version **1**
(`tools/pack_assets.cpp`). Manifest is:

1. Media paths regex-scraped from `visuals.json` (png/jpg/webp/bmp/wav/ogg/flac), plus the
   `visuals.json` file itself
2. CursorCrystal PNGs under `textures/CursorCrystal/PNG/{01,02,04,05,13,20,21,22,23,27,28}/`
3. Every regular file under `data/` (stored as `data/…` paths)

Not a blind walk of all of `assets/`. A path named in `visuals.json` that is missing on disk
fails the pack (current: `look_here` → `sfx/Cringemarine/look-here.wav`; runtime also uses
`SFX_LOOK_HERE_RELATIVE_PATH`).

## Source of truth (dev only)

Authoring happens under **`raw-assets/`** (gitignored). Do not reference `raw-assets/`
from the game binary.

## Runtime load order

1. Prefer `assets.dat` next to the exe (portable / installer / Release).
2. Optional loose `assets/` + `data/` for local Debug: pass `--loose-assets`, or rely on
   the pack-open fallback under `AOA_RUNTIME_ROOT` (`src/core/asset_store.*`).

There is no `AOA_DATA_DIR` env var.

## Still open (M5)

Richer pack tooling (CRUD, audio-specific packs such as `.adp`) and shipping builds
that never fall back to loose trees.

See [docs/DECISIONS.md](../docs/DECISIONS.md), [docs/BUILD.md](../docs/BUILD.md),
[docs/LOCKSTEP.md](../docs/LOCKSTEP.md).
