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

1. `run_visibility_system` — fog explored/visible per player slot
2. `run_worker_system` — gather / auto-worker brain (skipped when `ManualControlTag` is set)
3. `run_worker_deposit_system`
4. `run_enemy_militia_ai` — `EnemyTag` militia only; player militia has no auto-attack AI
5. `run_attack_chase_system`
6. `run_movement_system`
7. `run_melee_contact_system`
8. `run_combat_system` — same-tick damage sorted by entity id (see [DECISIONS.md](DECISIONS.md))
9. `run_death_cleanup`

Disconnected-player AI is **not** a gameplay system. `LockstepSession` injects `generate_ai_commands_for_slot()` before `Simulation::tick()` while in AI fallback.

Constraints:

- One concern per system file when it grows.
- No heap allocation inside hot tick paths unless profiling says otherwise.
- Stable iteration / sort keys for anything that affects combat or hashing.

## Entities

- Created during scenario setup or spawning — not every frame.
- Prefer tags + views over storing entity IDs in components when possible.
- Scenario roles for the harness resolve via tags + `PlayerSlot` in `find_scenario_entity()`. Slot 0 roles: `player_worker`, `player_militia`, `town_center`. Slot 1 roles: `player2_worker`, `player2_militia` (alias `enemy_militia`), `player2_town_center`. Full table: [HARNESS.md](HARNESS.md).

## Fog of war

`FogOfWarState` lives on the world entity (`src/sim/components/fog_of_war.hpp`). Arrays are planar per slot:

`index = player_slot * width * height + y * width + x`

| Field | Meaning |
|-------|---------|
| `explored` | Ever seen by that slot |
| `visible` | Currently in vision this tick |
| `memory_tiles` / `memory_forest_wood` | Last seen terrain/wood for explored cells |

`run_visibility_system` clears `visible`, then reveals circles from living `PlayerOwnedTag` units/buildings sorted by snapshot key. Vision range comes from archetype `vision_range` when set, else defaults in `src/core/constants.hpp` (worker 3, unit 4, structure 6, town center 8), plus `FOG_VISION_RADIUS_TILE_PADDING` (0.35).

Gameplay uses fog for attack targeting (`is_opponent_entity_visible_to_slot`). Render helpers also distinguish shroud vs live vision. `compute_state_hash` mixes fog width/height, `explored`, and memory for explored cells. It does **not** mix `visible`; that plane is rebuilt every tick.

Codepaths: `src/sim/systems/visibility_system.*`, hash loop in `gameplay_systems.cpp`.

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
2. Singleplayer: `enqueue_player_command` sets `execute_tick = tick_count + PLAYER_COMMAND_DELAY_TICKS` (1) when unset.
3. Lockstep: `LockstepSession::submit_local_command` uses `LOCKSTEP_COMMAND_DELAY_TICKS` (2), buffers the command in an outbox, and only enqueues after the matching `TickInputBatch` is sent and remote batches are ready. See [LOCKSTEP.md](LOCKSTEP.md).
4. Render reads the registry after sim ticks; use `interpolation_alpha` from `FixedTimestepLoop` / lockstep render timing and `snapshot_world_positions_for_render()` for visuals only.

Details: [DECISIONS.md](DECISIONS.md) (player commands), [HARNESS.md](HARNESS.md) (hash asserts).

## Live controls (graphical)

`GameInput` (`src/app/game_input.cpp`) turns picks into `PlayerCommand`s. Constants are in `src/core/constants.hpp`.

| Input | Effect |
|-------|--------|
| LMB click | Select local-slot unit, then building, then forest cell; empty click clears |
| LMB drag ≥ `SELECTION_BOX_DRAG_THRESHOLD_PX` (6) | Box-select local-slot units |
| Shift / Ctrl while selecting | Add / Toggle (`SelectionModifyMode`); plain click is Replace |
| RMB with units selected | Priority: opponent attack → forest gather → TC deposit → move to cell |
| `W` with Town Center selected | `SpawnWorker` (`SPAWN_WORKER_HOTKEY`) |
| Arrows / screen edge (`CAMERA_EDGE_SCROLL_MARGIN_PX` = 24) | Pan camera |
| Mouse wheel | Zoom Classic camera about **window center** (not cursor) |
| Escape | Close window (`application.cpp`) |

## Command apply

`CommandQueue::apply_pending` runs at the start of `Simulation::tick()` for commands whose `execute_tick` equals the new tick.

| Rule | Detail |
|------|--------|
| Types | `Move=0`, `Attack=1`, `Gather=2`, `Deposit=3`, `SpawnWorker=4` (`player_command.hpp`) |
| Local enqueue | Assigns `sequence = next_sequence_++` |
| Network enqueue | Keeps wire `sequence`; bumps `next_sequence_` past it |
| Same-tick order | Sort by `sequence`, then `player_slot`, then `type` |
| Ownership | Units carry `PlayerSlot`; selection/commands filter by local slot |
| Manual control | Player orders set `ManualControlTag`; auto-worker brain skips unless a `GatherTarget` remains |
| Attack | Target must be `EnemyTag` or an opponent slot (`is_valid_attack_target`), alive, and visible under fog |
| Gather | Unit must have `WorkerUnitTag`; cell must be forest with wood > 0 |
| Deposit command | Paths workers to an adjacent walkable TC cell; wood transfers in `run_worker_deposit_system` when Chebyshev distance ≤ 1 |
| SpawnWorker | Costs `town_center.spawn_worker_wood_cost` (50 in shipped JSON); needs a free adjacent cell |

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
- Stats alone do not spawn content. `load_test_scenario` still hard-codes Earth positions and roles in C++ (slot 0 and slot 1).
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
