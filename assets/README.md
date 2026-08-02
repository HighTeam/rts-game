# Shipped assets (`assets/`)

Runtime assets for Age of Affinities. **Committed and copied next to the executable at build time** (see `CMakeLists.txt` POST_BUILD).

## Layout

| Path | Content |
|------|---------|
| `music/` | Background music (interim: direct copies from `raw-assets/music/`) |
| `sfx/` | Sound effects (interim copies) |
| `textures/` | Sprites / tile art (interim copies) |
| `models/` | 3D models (interim copies) |
| *(future)* | `.dat` / `.adp` binary packs |

## Source of truth (dev only)

Authoring happens under **`raw-assets/`** (gitignored). Do not reference `raw-assets/` from the game binary.

## Serialization (planned)

Today: plain files in this tree (often direct copies from `raw-assets/`).

Later (M5): immutable binary packs (`.dat`, `.adp`, …) built by a dedicated pack tool. The game will load packs only.

See [docs/DECISIONS.md](../docs/DECISIONS.md).
