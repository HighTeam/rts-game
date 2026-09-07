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
| Asset pipeline (raw vs shipped) | M5 Packaging | Decided — `assets.dat` packs now; M5 = tooling polish — see below |
| Combat same-tick resolution | M1 | Done — `EntitySnapshotKey` sort; skip dead targets same tick |
| Unit position model | M1 → M2 prep | Done — `GridPosition` + sub-tile `Fixed` segments |
| Player militia auto-AI | Pre-M2 harness | Done — removed; only enemy militia auto-attacks |
| World object taxonomy (Option A) | M1 prep | Decided — see [`docs/TAXONOMY.md`](TAXONOMY.md) |
| Disconnect / pause policy | M2 prep | Done — AI takeover on disconnect; see below |
| Host migration policy | M3 | Done — no migration; see below |
| SP save / load | M3 persistence | Done — `.aoa` beside exe; see below |
| Lobby / lockstep version gate | M3 lobby | Done — exact `GAME_VERSION` match; see below |

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

Two top-level folders; authoring stays in **`raw-assets/`**, committed sources in **`assets/`** + **`data/`**.

| Folder | Purpose | In repo / release |
|--------|---------|-------------------|
| **`raw-assets/`** | Source art: raw audio, music, textures, models, etc. | **No** — `.gitignore`; local/dev only |
| **`assets/`** + **`data/`** | Committed runtime sources | **Yes** in repo; packed at build time |
| **`assets.dat`** | Immutable pack next to the exe (`aoa_pack_assets` POST_BUILD) | **Yes** in Release / portable / installer |

**Now (alpha_v0.2+):** game prefers `assets.dat` beside the exe (`AssetStore`); Debug may still fall back to loose trees via `AOA_RUNTIME_ROOT`. Release POST_BUILD removes loose `assets/` / `data/` from the output dir.

**Still open (M5 packaging polish):**

- Richer pack CRUD / audio-specific packs (e.g. **`.adp`** — name TBD)
- Drop loose-tree fallback for shipping builds only

See [assets/README.md](../assets/README.md), [BUILD.md](BUILD.md), [LOCKSTEP.md](LOCKSTEP.md).

---

## Combat same-tick resolution

When multiple units attack on the **same sim tick**:

1. Collect attackers, sort by **`EntitySnapshotKey`** (player slot + category + ordinal — stable across snapshot restore; not raw `entt::entity` id).
2. Apply damage **in that order**.
3. **Skip** attacks against targets already at `health <= 0` this tick (before `run_death_cleanup`).

No mutual “double KO” on the same tick when one hit would kill first. Cooldowns still tick down per attacker as before. Chase / melee contact use the same snapshot-key sort.

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
| Local delay | `PLAYER_COMMAND_DELAY_TICKS = 1` (SP / harness). Lockstep uses **`LOCKSTEP_COMMAND_DELAY_TICKS = 2`** instead — it does not stack both (see [LOCKSTEP.md](LOCKSTEP.md)) |
| Wire format | Compact binary: sequence, execute tick, player slot, type, unit id list, payload (grid cell or attack target entity id) — see `src/sim/player/player_command.hpp` |
| Pick → command | Screen pick runs locally; the **semantic result** (cell, entity id) is stored in the command payload for lockstep |
| Input log | `CommandQueue::input_log()` retains every command from game start (M2 reconnect / save-load reuse) |

---

## Disconnect / pause policy

When a human player disconnects during a multiplayer match:

1. **No global pause** — the match continues for the remaining connected peer when that peer is the **host**. The sim **never freezes** waiting for reconnect.
2. **Immediate AI takeover (host only)** — unexpected drop enables AI for that slot (2p: global `enter_ai_fallback`; multi-peer: `handle_slot_ai_takeover`). UI / chat announces disconnect + AI.
3. **Reconnect grace** — for `LOCKSTEP_RECONNECT_GRACE_MS` (30s), the host still accepts reconnect; AI keeps playing until the human returns.
4. **Client loses host** — the client **stops playing**, retries reconnect automatically, and exits if the host is gone. The client does **not** simulate the host with AI.
5. **Reconnect** — on reconnect, AI control **stops immediately** for that player; they resume issuing commands from their client. Catch-up uses snapshot + input log + `ResyncReady` handshake (snapshot v26 includes map pings / pending orders). On `main`, match-outcome fields from resign are **not** in the snapshot or state hash (draft **#85**); see [LOCKSTEP.md](LOCKSTEP.md).
6. **Resign** (`PlayerResign`) — slot marked resigned, **no** AI takeover, slot not reclaimable; player may stay in the match. On `main`, host-side resign / `ResyncReady` still trust payload slot without sender binding (draft **#79**); see [LOCKSTEP.md](LOCKSTEP.md).
7. **Match pause** (`MatchPause`) — peers stop live tick advance while paused. On `main`, paused peers can still pipeline `TickInputBatch` ahead of the barrier (draft **#83**); see [LOCKSTEP.md](LOCKSTEP.md).
8. **Host leave** — host `request_voluntary_resign` sends `HostEnded`; clients treat the match as over (no reconnect to a dead host).

Poor connection (late/missing input batches) must not freeze the whole match — use input delay buffering and per-player stall policy (future tuning); only session-wide halt on desync or host loss. Details: [LOCKSTEP.md](LOCKSTEP.md).

---

## HUD style (alpha_v0.2.1)

In-game / settings cycle (`HudStyle` in `src/core/constants.hpp`, `GameMenuAction::CycleHudStyle`):

| Style | Behavior |
|-------|----------|
| **Default** | Custom HUD layout; minimap draws the Default double-diamond stroke |
| **AoE Style** | Classic AoE panel layout (`hud_is_classic_aoe`); Default double-diamond stroke is **hidden** (`hud_overlay.cpp`, tip `bfc6a29`) |

Persisted with other app settings (`app_settings.cpp`). Not a sim concern — render / menu only.

---

## Save / load (M3)

**Decision: singleplayer file saves reuse the reconnect snapshot codec; multiplayer coordinated load waits.**

| Topic | Choice |
|-------|--------|
| Extension / folder | `.aoa` under `<exe>/saves/` (`SAVE_EXTENSION`, `default_saves_directory`) |
| Encode | `encode_save_bytes` → `encode_sim_snapshot(sim, false)` — snapshot v26, **no** input log |
| Load path | `load_simulation_from_file` → `Simulation::apply_snapshot` |
| SP autosave | `autosave.aoa` every `AUTOSAVE_INTERVAL_MS` (5 min) |
| MP | Host may write local `autosave.mp.aoa`; UI **Load** disabled in lockstep; coordinated multi-peer load still open |
| Match outcome | Same v26 gap as reconnect (draft **#85**) — resigned / finished fields not persisted |

Developer checklist: [BUILD.md](BUILD.md) (Save / load). Snapshot wire details: [LOCKSTEP.md](LOCKSTEP.md).

---

## Version gate (lobby + lockstep)

**Decision: exact `GAME_VERSION` string match; owner bumps only.**

| Topic | Choice |
|-------|--------|
| Source of truth | `GAME_VERSION` in `src/core/constants.hpp` (also shown on the main menu) |
| Lobby wire | `LobbyJoinMessage.version` text field (`lobby_wire.cpp`) |
| Lobby reject | Host sends `LobbyReject` with `LOBBY_VERSION_MISMATCH_MESSAGE` ("Version mismatch") |
| Lockstep join | Host requires `join->version == GAME_VERSION` |
| Compatibility range | None — exact match only until a wider policy is decided |

See [LOCKSTEP.md](LOCKSTEP.md) (Version gate) and [BUILD.md](BUILD.md).

---

## Host migration policy (M3)

**Decision: no host migration.**

- If the host drops, clients do not promote another peer to host.
- Clients keep the existing reconnect-then-exit path from the disconnect policy above.
- Rationale: lockstep authority stays on one host; migrating mid-match needs lobby rebind, snapshot ownership, and peer re-auth that we are not taking on for M3. Revisit only if dedicated-server or host-transfer becomes a product requirement.
