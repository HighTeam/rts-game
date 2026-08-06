# ECS conventions

Age of Affinities uses [EnTT](https://github.com/skypjack/entt) for simulation state. Rendering and UI read sim data; sim code never touches SFML or OpenGL.

## Layout

| Kind | Location | Role |
|------|----------|------|
| Components | `src/sim/components/` | Plain data (POD structs), one type per file |
| Systems | `src/sim/systems/` | Per-tick logic; free functions taking `entt::registry&` |
| Player commands | `src/sim/player/` | Tick-scoped orders applied before systems |
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

1. `run_worker_system` — gather / auto-worker brain (skipped when `ManualControlTag` is set)
2. `run_worker_deposit_system`
3. `run_enemy_militia_ai` — enemy militia only; player militia has no auto-attack AI
4. `run_attack_chase_system`
5. `run_movement_system`
6. `run_combat_system` — same-tick damage sorted by entity id (see [DECISIONS.md](DECISIONS.md))
7. `run_death_cleanup`

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
2. `enqueue_player_command` sets `execute_tick = tick_count + PLAYER_COMMAND_DELAY_TICKS` (default 1) when unset.
3. Render reads the registry after sim ticks; use `interpolation_alpha` from `FixedTimestepLoop` and `snapshot_world_positions_for_render()` for visuals only.

Details: [DECISIONS.md](DECISIONS.md) (player commands), [HARNESS.md](HARNESS.md) (hash asserts).

## Live controls (graphical)

`GameInput` (`src/app/game_input.cpp`) turns picks into `PlayerCommand`s. Constants are in `src/core/constants.hpp`.

| Input | Effect |
|-------|--------|
| LMB click | Select player unit, then building, then forest cell; empty click clears |
| LMB drag ≥ `SELECTION_BOX_DRAG_THRESHOLD_PX` (6) | Box-select player units |
| Shift / Ctrl while selecting | Add / Toggle (`SelectionModifyMode`); plain click is Replace |
| RMB with units selected | Priority: enemy attack → forest gather → TC deposit → move to cell |
| `W` with Town Center selected | `SpawnWorker` (`SPAWN_WORKER_HOTKEY`) |
| Arrows / screen edge (`CAMERA_EDGE_SCROLL_MARGIN_PX` = 24) | Pan camera |
| Mouse wheel | Zoom Classic camera about **window center** (not cursor) |
| Escape | Close window (`application.cpp`) |

Submit path:

- Singleplayer → `Simulation::enqueue_player_command`
- Lockstep → `LockstepSession::submit_local_command` (when `GameInput` has a session)

## Command apply

`CommandQueue::apply_pending` runs at the start of `Simulation::tick()` for commands whose `execute_tick` equals the new tick.

| Rule | Detail |
|------|--------|
| Types | `Move=0`, `Attack=1`, `Gather=2`, `Deposit=3`, `SpawnWorker=4` (`player_command.hpp`) |
| Local enqueue | Assigns `sequence = next_sequence_++` |
| Network enqueue | Keeps wire `sequence`; bumps `next_sequence_` past it |
| Same-tick order | Sort by `sequence`, then `player_slot`, then `type` |
| Manual control | Player orders set `ManualControlTag`; auto-worker brain skips unless a `GatherTarget` remains |
| Attack | Target must have `EnemyTag` and health > 0 |
| Gather | Unit must have `WorkerUnitTag`; cell must be forest with wood > 0 |
| Deposit command | Paths workers to an adjacent walkable TC cell; wood transfers in `run_worker_deposit_system` when Chebyshev distance ≤ 1 |
| SpawnWorker | Costs `town_center.spawn_worker_wood_cost` (50 in shipped JSON); needs a free adjacent cell; calls `spawn_player_worker` |

Codepaths: `src/sim/player/command_queue.cpp`, `player_commands.cpp`, `src/sim/spawn/unit_spawn.cpp`.

## Data-driven content

Civ/unit/building **stats** load from JSON into components at spawn/load time.

| Piece | Location |
|-------|----------|
| Archetype rows | `data/archetypes/*.json` |
| Civ manifest | `data/civs/earth.json` (**hard-coded** path in the loader today) |
| Loader | `src/data/content_loader.cpp` → `ContentDatabase` |
| Runtime pack | `ContentPack` component on the world entity |

- **Component types** = schema (fields all units share).
- **JSON** = values per archetype / civ.
- Stats alone do not spawn content. `load_test_scenario` still hard-codes Earth positions and roles in C++.
- Combat currently subtracts `melee_attack` only; `melee_armor` / `pierce_*` are parsed but unused in `run_combat_system`.
- Design terms and field list: [TAXONOMY.md](TAXONOMY.md).

## Adding a new gameplay feature (checklist)

1. Add or extend components under `src/sim/components/`.
2. Add logic under `src/sim/systems/` and call it from `run_gameplay_systems()` in the correct order.
3. If players can issue the action, add a `PlayerCommandType` + apply path in `CommandQueue` (not direct from the render loop).
4. Keep sim deterministic (fixed-point, stable iteration order).
5. Extend `compute_state_hash` if the new state can diverge between clients.
6. Add or update a harness scenario when behavior is hash-sensitive — see [HARNESS.md](HARNESS.md).
7. Wire render/UI in separate modules that query the registry read-only.
