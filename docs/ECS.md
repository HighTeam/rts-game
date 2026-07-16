# ECS conventions

Age of Affinities uses [EnTT](https://github.com/skypjack/entt) for simulation state. Rendering and UI read sim data; sim code never touches SFML or OpenGL.

## Layout

| Kind | Location | Role |
|------|----------|------|
| Components | `src/sim/components/` | Plain data (POD structs), one type per file |
| Systems | `src/sim/systems/` | Per-tick logic; free functions taking `entt::registry&` |
| Registry owner | `Simulation` | Creates entities, calls systems from `tick()` |

## Components

- **Data only** — no methods beyond trivial constructors/defaults.
- **PascalCase** type names (`MotionState`, `UnitStats`).
- **Tags** — empty structs in `tags.hpp` for filtering (`WorldTag`, `UnitTag`).
- **Deterministic sim** — use `math::Fixed` for positions, health, timers, etc. Floats belong in render interpolation, not authoritative sim state.

## Systems

- One concern per system file when it grows (`movement_system`, `combat_system`).
- M0 uses `run_sim_systems()` as a thin dispatcher; split into named systems as M1 adds behavior.
- Systems run in a **fixed order** each sim tick (document order in `sim_systems.cpp` when non-obvious).
- No heap allocation inside hot tick paths unless profiling says otherwise.

## Entities

- Created during map load, spawning, or `Simulation` setup — not every frame.
- Prefer explicit tags + views over storing entity IDs in components when possible.

## Simulation loop

```
Simulation::tick()
  → run_sim_systems(registry)   // authoritative state updates
  → (later) hash world state for desync detection
```

Render loop reads component snapshots or derived draw lists **after** sim ticks; use `interpolation_alpha` from `FixedTimestepLoop` for visuals only.

## Data-driven content (M1+)

Civ/unit/building **stats** load from JSON into components at spawn/load time — e.g. `UnitStats`, `BuildingStats` populated from data files, not hardcoded C++ per civ.

- **Component types** = schema (fields all units share).
- **JSON** = values per unit/building/civ.
- Loading layer lives in M1 (`data/` + loader); ECS doc only defines where those values land (components on entities).

## Adding a new gameplay feature (checklist)

1. Add or extend components under `src/sim/components/`.
2. Add a system under `src/sim/systems/` and call it from `run_sim_systems()` (or a dedicated pipeline).
3. Keep sim deterministic (fixed-point, stable iteration order).
4. Wire render/UI in separate modules that query the registry read-only.
