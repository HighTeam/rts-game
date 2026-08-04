# RTS Project — v1.0/MVP Backlog

How to use this: each `## Milestone` = a GitHub Milestone. Each `###` = an Issue (Epic) with the checklist as its issue body — GitHub renders `- [ ]` as tickable checkboxes and tracks % complete automatically. Split any checklist item into its own Issue if it grows past ~1 sitting of work. `[BLOCKING]` = nothing downstream can start until this closes; prioritize these ruthlessly over anything that *feels* more fun to build.

Suggested labels: `engine`, `netcode`, `content`, `ui`, `tooling`, `blocking`.

---

## M0 — Foundations

*Goal: a window opens, renders something, and the sim/render split exists. No gameplay yet.*

### [BLOCKING] Build & repo setup

- [ ] CMake project (don't fight VS2022 .sln files directly — CMake + VS2022 generator, or CMake + Claude Code, either way CMake is the source of truth)
- [ ] vcpkg or manual setup for: SFML, OpenGL loader (glad/glew), EnTT, ENet, a JSON lib (nlohmann/json)
- [ ] Private GitHub repo, .gitignore, branch strategy (even solo: main + short-lived feature branches, so history stays bisectable when a desync bug shows up later)
- [ ] CI: a GitHub Action that just builds on push (catches "works on my machine" early, costs you nothing since GitHub Actions free tier covers a solo private repo easily)

### [BLOCKING] Core loop skeleton

- [ ] Fixed-timestep sim loop decoupled from render loop (pick a tick rate — 20/sec is a reasonable AoE2-era default — and write this so render can interpolate between ticks later)
- [ ] Window + OpenGL context via SFML, clear-color triangle to confirm the pipeline
- [ ] Fixed-point math library (Q16.16 or similar) — do this now, not when you "need" it; retrofitting fixed-point into float-based sim code later is a rewrite, not a patch
- [ ] Headless build mode (sim runs with no window) — needed for automated desync/regression testing later, cheap to add now

### ECS skeleton

- [ ] EnTT registry wired into the sim loop
- [ ] Component conventions doc (even a short one): what's a component vs. a system, naming, where data-driven civ stats will plug in later

---

## M1 — Single-civ core simulation (no networking, no polish)

*Goal: one civ, one resource, one building, one unit, deterministic and testable.*

### [BLOCKING] Deterministic sim core

- [x] Tile/grid map representation, single hardcoded test map
- [x] Resource node (wood) + gather/deposit loop for one worker unit
- [x] Town Center equivalent: spawns workers, is a valid attack target
- [x] One military unit: move, attack, die
- [x] A* pathfinding on the grid (16-way, forest blocked, enemy AI)
- [x] Per-tick state hash function (hash all sim-relevant component data every tick) — desync detector for M2
- [x] Combat same-tick tie-break (sorted entity id; no mutual kill same tick)

### Data-driven civ definitions [BLOCKING for M4]

- [x] Civ/unit/building stats loaded from JSON, not hardcoded in C++ classes
- [x] Archetype JSON + loader (`data/archetypes/`, `content_loader.cpp`) — M4 content authoring ready

### Headless regression harness

- [x] Scripted test scenarios (spawn X units, issue commands, run N ticks, assert final hash) — catches sim bugs before they become "why did multiplayer desync" bugs three weeks from now
- [x] Command-replay scenario (`earth_player_commands`) — replays gather/deposit via tick-scoped `PlayerCommand` queue

### Classic render pipeline (DE-style) — GitHub #26

*Goal: AoE2 DE-like look — 3D scene, locked isometric Classic camera, pan + zoom, basic shaders. Sim stays grid-based.*

- [x] Camera module: Classic mode — fixed isometric azimuth, pan, smooth zoom (no orbit)
- [x] Grid → isometric world projection
- [x] 3D terrain + entity draw pass
- [x] Basic shaders (lighting, team-color hook, selection outline hook)
- [x] `CameraView` enum: Classic implemented; Full 3D stub for later
- [x] Settings **Camera view** dropdown documented for M5 (Classic / Full 3D; Full 3D becomes default when ready)

See [docs/DECISIONS.md](DECISIONS.md) for render + camera decisions.

---

## M2 — Lockstep networking, 2 players

*Goal: two instances of the game, on two machines (or localhost), play a synced match. This is the highest-risk milestone in the project — budget slack here, steal it from later content milestones if needed, not from here.*

### [BLOCKING] Transport layer

- [x] ENet integration: connect, send/receive reliable messages
- [x] Message serialization format for inputs (compact — player commands, not state) — wire format in `src/sim/player/player_command.hpp` + `src/net/net_message.hpp`; lockstep batches wired

### [BLOCKING] Lockstep sync

- [x] Turn/tick-based input collection: every client sends inputs for tick N; sim doesn't advance until all inputs for N are received
- [x] Input delay buffer — uses `PLAYER_COMMAND_DELAY_TICKS` via `next_command_execute_tick()`
- [x] Full input log from game start — `CommandQueue::input_log()` + network `TickInputBatch`
- [x] Live desync detection: exchange per-tick state hashes between clients
- [ ] Wire lockstep into graphical play (window mode still uses local-only tick)

### Reconnect

- [ ] On reconnect: new client receives current sim state snapshot + resumes receiving live inputs; if you keep the full input log, replaying it is mostly free
- [x] Disconnect policy decided: no global pause; AI takeover on disconnect; player resumes on reconnect — [docs/DECISIONS.md](DECISIONS.md)

### Networking test scenarios

- [x] Two local instances (localhost) as the daily dev-loop test — `--lockstep-host` / `--lockstep-join`
- [ ] One test with actual two-machine LAN play before calling M2 done — localhost hides real latency/packet-loss bugs

---

## M3 — Scale to 8 players + save/load

*Goal: same architecture as M2, proven at target player count, plus persistence.*

### 8-player scaling

- [ ] Bandwidth/perf test with 8 simulated clients (can fake most as bots on one machine for dev testing)
- [ ] Host migration policy decision: if host disconnects, does the match end, or does another client take over hosting? (Decide now — affects lobby/network code shape; retrofitting host migration later is painful)

### Save/load

- [ ] Serialize full sim state (should be straightforward if ECS components are clean data, per M1 design)
- [ ] Singleplayer save/load
- [ ] Multiplayer save/load (save = snapshot + input log up to that point, same mechanism as reconnect — reuse, don't rebuild)

---

## M4 — Civilization content ×4

*Goal: replicate the proven single-civ pattern three more times. This should be content authoring, not new engine work — if it isn't, that's a signal M1's data-driven design needs revisiting before going further.*

### Per civilization (×4: Water, Earth, Fire, Air)

- [ ] Unique unit roster defined in data files
- [ ] Unique technologies / tech tree defined in data files
- [ ] Civ-specific bonuses (the "hook" system for bonuses needs to be designed once, generically, then reused per civ — e.g. a bonus is "modify stat X by Y% under condition Z," not four hardcoded special cases)
- [ ] Balance pass (expect this to be iterative and never really "done" — timebox it, don't let it become infinite polish)

### Content authoring tooling

- [ ] Consider whether you need any tooling to author civ JSON faster, or whether hand-editing is fine at this scale (4 civs, MVP-depth trees) — don't over-invest in tools for content you're only authoring once

---

## M5 — Menus, UI, packaging

*Goal: the client-facing shell around the game that's been working headless/minimal this whole time.*

### Main menu

- [ ] Singleplayer / Multiplayer / Settings / Account / Credits / Leave
- [ ] Logo + menu art/PNGs

### Game creation / lobby menu

- [ ] Map selection (from list, not procedural — as you scoped)
- [ ] Resource amount, unit cap, map size settings
- [ ] Civ selection per player (including Random)
- [ ] Ready-check before host can start

### Settings screen

- [ ] Video, Audio, Controls tabs (AoE2 DE's model is a reasonable reference)
- [ ] Account tab (nickname-only for MVP, as scoped)

### Basic AI opponent

- [ ] Rule-based/scripted AI is sufficient for MVP — do not attempt anything ML-based; a simple build-order + attack-timing script that's "predictable" (as you said) is correct scope

### End-game analytics

- [ ] Define what's actually being measured (APM? resources gathered? match duration?) before building the pipe — this is a small task once scoped, easy to scope-creep if not

### Packaging

- [ ] **`raw-assets/`** (gitignored) vs **`assets/`** (shipped) — interim: direct copies; later: immutable binary packs
- [ ] Asset pack format (`.dat`-style; audio `.adp` TBD) + standalone pack tool (CRUD entries)
- [ ] Game loads `assets/` packs only — flagged as a help-needed area; tackle in M5, not earlier
- [ ] Windows installer/build packaging for distribution
- [ ] Minimal static download page (website) — do not over-invest here for MVP

---

## Cross-cutting: things to decide once, early, because they're expensive to change later

- [ ] Fixed-point math (M0) — expensive to retrofit
- [ ] Data-driven civ/unit/building definitions (M1) — expensive to retrofit
- [ ] Host migration policy (M3) — affects network code shape
- [ ] Disconnect/pause policy (M2) — affects UI and netcode together
- [ ] Tick rate — pick once, changing it later touches balance, netcode timing, and input feel simultaneously
- [x] Render style — AoE2 DE-like 2.5D/3D hybrid (M1 #26)
- [x] Camera view — Classic now, Full 3D later default (M1 #26 + M5 Settings)

---

## Suggested weekly pacing (10-11 weeks)

| Week | Milestone |
|------|-----------|
| 1 | M0 |
| 2-3 | M1 |
| 4-5 | M2 *(protect this — steal time from M4 content if you fall behind, not from this)* |
| 6 | M3 |
| 7-9 | M4 |
| 10-11 | M5 |

If you're behind schedule at week 6, the item to cut first is tech-tree depth in M4 (fewer techs per civ, still all 4 civs present) — not networking robustness, and not the data-driven architecture in M1. Content is reversible to trim; architecture isn't.
