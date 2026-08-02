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
