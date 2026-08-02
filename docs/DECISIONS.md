# Early decisions tracker

GitHub issue **#21 — Cross-cutting early decisions** is a **meta epic**, not a milestone task you implement in one PR.

Use it as a **checklist of decisions** that must be locked in before they become expensive to change. Each item is **resolved inside the milestone issue where the work happens**, then checked off on #21.

| Decision | Resolved in | Status |
|----------|-------------|--------|
| Fixed-point math (Q16.16) | M0 #2 Core loop | Done — `src/math/fixed.hpp` |
| Sim tick rate (20 Hz) | M0 #2 Core loop | Done — `SIM_TICKS_PER_SECOND` in `src/core/constants.hpp` |
| Data-driven civ JSON | M1 #5 | Done — `data/civs/earth.json` + loader |
| Render style (DE-like hybrid) | M1 #26 Render | Decided — see below |
| Camera modes (Classic / Full 3D) | M1 #26 + M5 Settings | Decided — Classic first; Full 3D later default |
| Asset pipeline (raw vs shipped) | M5 Packaging | Decided — see below |
| Combat same-tick resolution | M1 | Done — sorted entity id; skip dead targets same tick |
| Unit position model | M1 → M2 prep | Grid cells now; sub-tile `Fixed` before netcode — see below |
| Disconnect / pause policy | M2 | Pending |
| Host migration policy | M3 | Pending |

Do **not** duplicate implementation work on #21. When a decision lands, update this table (optional), check the box on #21, and reference the PR that introduced it.

---

## Render style — AoE2 DE-like hybrid

**Goal:** nostalgic isometric look with a modern 3D engine underneath — not flat AoE2/AoK sprites, not AoE2 HD’s pure 2D stack.

| Layer | Choice |
|-------|--------|
| Sim | Grid-based, view-agnostic (unchanged) |
| Scene | 3D (meshes / billboards in 3D space) |
| Camera default | Fixed isometric azimuth — **no free orbit in Classic** |
| Polish | Shaders (team tint, selection, water/fog later), smooth **zoom** |
| Reference | Age of Empires II **Definitive Edition** |

Evolution path: top-down debug quads (M1 prototype) → isometric projection → 3D scene + Classic camera + zoom/shaders.

---

## Camera view setting

Players choose **Camera view** in Settings (Video tab):

| Mode | Behavior | When |
|------|----------|------|
| **Classic** | Locked isometric angle; pan + zoom only | **Implement now** (#26); **default until Full 3D ships** |
| **Full 3D** | Orbital / advanced camera (TBD) | **Later**; becomes **default** once ready |

Classic mode is the only implemented mode for now. Full 3D is reserved in settings UI and render architecture but not built yet.

---

## Asset pipeline

Two top-level folders; only **`assets/`** is shipped / committed for distribution builds.

| Folder | Purpose | In repo / release |
|--------|---------|-------------------|
| **`raw-assets/`** | Source art: raw audio, music, textures, models, etc. | **No** — `.gitignore`; local/dev only |
| **`assets/`** | Shipped runtime assets — **immutable serialized packs** (goal) | **Yes** |

**Now:** direct file copies from `raw-assets/` into `assets/` (same paths where practical, e.g. `assets/music/`).

**Later (M5 packaging):**

- Binary pack format (`.dat`-style containers; audio packs e.g. **`.adp`** — *audio data pack*, name TBD)
- Standalone **pack tool** to create/read/update/delete entries inside packs (CRUD) — not hand-editing shipped blobs
- Game loads only from `assets/` packs, not `raw-assets/`

See [assets/README.md](../assets/README.md).

---

## Combat same-tick resolution

When multiple units attack on the **same sim tick**:

1. Collect attackers, sort by **`entt::entity` id** (stable, lockstep-safe).
2. Apply damage **in that order**.
3. **Skip** attacks against targets already at `health <= 0` this tick (before `run_death_cleanup`).

No mutual “double KO” on the same tick when one hit would kill first. Cooldowns still tick down per attacker as before.

---

## Unit position model

| Phase | Sim | Render |
|-------|-----|--------|
| **M1 (now)** | Integer **grid cell** per unit; one occupier per cell for movement | Entity drawn at cell; 3D prism overlay |
| **Before M2** | **`Fixed` world x/y** on tile plane; move along path segments between waypoints | Interpolate with `interpolation_alpha` (see `docs/ECS.md`) |
| **Map** | Tile grid stays (terrain, forests, blocking) | Isometric 3D tiles unchanged |

AoE-style “free movement on a tile” means **sub-tile fixed-point coordinates**, not float Minecraft-style doubles — keeps determinism for lockstep.
