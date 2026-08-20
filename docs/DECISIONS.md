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
| Unit position model | M1 → M2 prep | Done — `GridPosition` + sub-tile `Fixed` segments |
| Player militia auto-AI | Pre-M2 harness | Done — removed; only enemy militia auto-attacks |
| World object taxonomy (Option A) | M1 prep | Decided — see [`docs/TAXONOMY.md`](TAXONOMY.md) |
| Disconnect / pause policy | M2 prep | Done — AI takeover on disconnect; see below |
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

1. Collect attackers, sort by **`EntitySnapshotKey`** via `sort_entities_by_snapshot_key` (player slot, category, ordinal — stable across reconnect; not raw `entt::entity` id).
2. Apply damage **in that order**.
3. **Skip** attacks against targets already at `health <= 0` this tick (before `run_death_cleanup`).

No mutual “double KO” on the same tick when one hit would kill first. Cooldowns still tick down per attacker as before.

---

## Unit position model

| Phase | Sim | Render |
|-------|-----|--------|
| M1 early | Integer **grid cell** per unit; one occupier per cell | Entity drawn at cell |
| **M2 prep (current)** | `GridPosition` plus **`Fixed` world x/y** on the tile plane; move along `MovePath` / `MoveSegment` between waypoints | Interpolate with `interpolation_alpha` (see [ECS.md](ECS.md)) |
| Map | Tile grid stays (terrain, forests, blocking) | Isometric 3D tiles unchanged |

AoE-style free movement on a tile means sub-tile fixed-point coordinates, not float doubles — keeps determinism for lockstep.

---

## Player commands (M2 prep)

| Topic | Choice |
|-------|--------|
| Application timing | Commands are **queued** with an `execute_tick` and applied at the **start** of that sim tick — never from the render/input loop directly |
| Local delay | `PLAYER_COMMAND_DELAY_TICKS = 1` (singleplayer/harness). Lockstep uses separate `LOCKSTEP_COMMAND_DELAY_TICKS = 2` — they do not stack. See [LOCKSTEP.md](LOCKSTEP.md). |
| Wire format | Compact binary: sequence, execute tick, player slot, type, unit id list, payload (grid cell or attack target entity id) — see `src/sim/player/player_command.hpp` |
| Pick → command | Screen pick runs locally; the **semantic result** (cell, entity id) is stored in the command payload for lockstep |
| Input log | `CommandQueue::input_log()` retains every command from game start (M2 reconnect / save-load reuse) |

---

## Disconnect / pause policy

When a human player disconnects during a multiplayer match:

1. **No global pause** — the match continues for the remaining connected peer when that peer is the **host**. The sim **never freezes** waiting for reconnect.
2. **Immediate AI takeover (host only)** — if the **client** disconnects, the host enables AI for player 2 **on the same tick** transport drops; UI shows “Player N left — AI playing (waiting for reconnect)”.
3. **Reconnect grace** — for `LOCKSTEP_RECONNECT_GRACE_MS` (30s), the host still accepts reconnect; AI keeps playing until the human returns.
4. **Client loses host** — the client **stops playing**, retries reconnect automatically, and exits if the host is gone. The client does **not** simulate the host with AI (host migration remains **M3**).
5. **Reconnect** — on reconnect, AI control **stops immediately** for that player; they resume issuing commands from their client. Catch-up uses snapshot + input log replay + `ResyncReady` handshake.

Poor connection (late/missing input batches) must not freeze the whole match — use input delay buffering and per-player stall policy (future tuning); only session-wide halt on desync or host loss.
