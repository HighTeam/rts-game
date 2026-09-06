# ECS conventions

Age of Affinities uses [EnTT](https://github.com/skypjack/entt) for simulation state. Rendering and UI read sim data; sim code never touches SFML or OpenGL.

## Layout

| Kind | Location | Role |
|------|----------|------|
| Components | `src/sim/components/` | Plain data (POD structs), one type per file |
| Systems | `src/sim/systems/` | Per-tick logic; free functions taking `entt::registry&` |
| Player commands | `src/sim/player/` | Tick-scoped orders applied before systems |
| Persistence | `src/sim/persistence/` | SP `.aoa` save/load via snapshot codec (see [BUILD.md](BUILD.md)) |
| Registry owner | `Simulation` | Loads content + test scenario, owns the queue, calls `tick()` |

## Components

- **Data only** — no methods beyond trivial constructors/defaults.
- **PascalCase** type names (`MotionState`, `UnitStats`).
- **Tags** — empty structs in `tags.hpp` for filtering (`WorldTag`, `UnitTag`, `ManualControlTag`).
- **Deterministic sim** — use `math::Fixed` for world positions, health, timers, path segments. Floats belong in render interpolation, not authoritative sim state.
- **Grid + world** — `GridPosition` is the discrete cell; `WorldPosition` holds sub-tile `Fixed` x/y for movement along path segments.

## Systems

`run_sim_systems()` dispatches gameplay, then recomputes the world state hash.

Gameplay order in `run_gameplay_systems()` (do not reorder casually — hashes and combat depend on it):

0. `tick_match_announcement_cooldowns` — chat / announcement cooldown timers on the world entity
1. `run_building_process_system` — train / research / age-up / construction progress (training that
   finishes at 100% but cannot spawn refunds only on pop-cap today; blocked spawn tiles leave the
   process stuck until draft PR **#77**). `issue_advance_age_order` only rejects an active process
   on the chosen Town Center — two TCs can age-up in parallel on `main` (draft fix PR **#83**)
2. `run_visibility_system` — fog explored / memory planes
3. `run_worker_system` — gather / auto-worker brain (skipped when `ManualControlTag` is set)
4. `run_worker_deposit_system`
5. `run_builder_system`
6. `run_extractor_mana_generation`
7. `run_garden_production`
8. `run_garrison_enter_system`
9. `run_enemy_militia_ai` — enemy militia only; player militia has no auto-attack AI
10. `run_attack_chase_system`
11. `run_movement_system`
12. `run_melee_contact_system` — slide attackers into melee range before damage
13. `run_combat_system` — same-tick damage sorted by `EntitySnapshotKey` (see [DECISIONS.md](DECISIONS.md))
14. `run_building_autoattack_system`
15. `run_projectile_system`
16. `run_death_cleanup`
17. Map ping tickdown — decrement / erase `MapPingList` entries on the world entity

Constraints:

- One concern per system file when it grows.
- No heap allocation inside hot tick paths unless profiling says otherwise.
- Stable iteration / sort keys for anything that affects combat or hashing.

## Entities

- Created during scenario setup or spawning — not every frame.
- Prefer tags + views over storing entity IDs in components when possible.
- Scenario roles for the harness (`player_worker`, `player_militia`, `enemy_militia`, `town_center`) resolve via tags in `find_scenario_entity()`.

## Simulation loop

```
Simulation::tick()
  → tick_count_++
  → CommandQueue::apply_pending(registry, tick)   // commands with execute_tick == tick
  → run_sim_systems(registry)
       → run_gameplay_systems(registry)
       → compute_state_hash(registry)             // FNV-1a into SimState.state_hash
```

Input / render path:

1. Local pick in `game_input` produces a semantic `PlayerCommand` (cell or target entity).
2. `enqueue_player_command` sets `execute_tick = tick_count + PLAYER_COMMAND_DELAY_TICKS` (default 1) when unset. Lockstep uses `LOCKSTEP_COMMAND_DELAY_TICKS` (2) via the session outbox — see [LOCKSTEP.md](LOCKSTEP.md).
3. Singleplayer / harness: render may read the registry after sim ticks; use `interpolation_alpha` from `FixedTimestepLoop` and `snapshot_world_positions_for_render()` for visuals only.
4. Graphical lockstep: draw from `LockstepSession::render_snapshot()`. On `main`, some UI-thread paths still touch the live registry without `simulation_access_mutex` while the background tick loop runs (draft fix PR **#87** — not merged).

Details: [DECISIONS.md](DECISIONS.md) (player commands), [HARNESS.md](HARNESS.md) (hash asserts), [LOCKSTEP.md](LOCKSTEP.md).

## Data-driven content

Civ/unit/building **stats** load from JSON into components at spawn/load time.

| Piece | Location |
|-------|----------|
| Archetype rows | `data/archetypes/*.json` |
| Civ manifest | `data/civs/earth.json` |
| Loader | `src/data/content_loader.cpp` → `ContentDatabase` |
| Runtime pack | `ContentPack` component on the world entity |

- **Component types** = schema (fields all units share).
- **JSON** = values per archetype / civ.
- Design terms: [TAXONOMY.md](TAXONOMY.md).

## Map pings

Player “look here” markers are sim state (hashed + snapshotted), not pure UI.

| Piece | Detail |
|-------|--------|
| Command | `PlayerCommandType::MapPing` (39) → `issue_map_ping_order` |
| Storage | `MapPingList` on the world entity (`cell`, `player_slot`, `ticks_remaining`) |
| Duration | `MAP_PING_DURATION_TICKS` = 10s × `SIM_TICKS_PER_SECOND` |
| Tickdown | End of `run_gameplay_systems()` (step 17 above) |
| Hash / snapshot | Included in `compute_state_hash` and snapshot v26 |
| SFX | `SfxEventKind::LookHere` → `sfx/Cringemarine/look-here.wav` (**file missing on `main`** — pack/CI fails until it lands) |

**UI path:** spyglass / pointer-mode HUD button toggles `pointer_targeting_mode_`; next world or minimap click submits the ping (`GameInput::submit_map_ping`). Esc cancels the mode. Same command path in SP and lockstep (delayed via the session outbox in MP).

## Adding a new gameplay feature (checklist)

1. Add or extend components under `src/sim/components/`.
2. Add logic under `src/sim/systems/` and call it from `run_gameplay_systems()` in the correct order.
3. If players can issue the action, add a `PlayerCommandType` + apply path in `CommandQueue` (not direct from the render loop).
4. Keep sim deterministic (fixed-point, stable iteration order).
5. Extend `compute_state_hash` if the new state can diverge between clients.
6. Add or update a harness scenario when behavior is hash-sensitive — see [HARNESS.md](HARNESS.md).
7. Wire render/UI in separate modules that query the registry read-only (lockstep graphical: prefer `render_snapshot()` and hold `simulation_access_mutex` for any live-registry read while the background tick loop runs).
