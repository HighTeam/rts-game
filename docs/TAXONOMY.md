# World object taxonomy

**Status:** Accepted (Option A, 2026-08-03)

Game-design names are separate from **ECS implementation** (`entt::entity`, components, systems).

## Decided terms (Option A)

| Concept | Term | Do not use in design docs |
|---------|------|---------------------------|
| Generic on map | **world object** | sim object, map object |
| JSON stat row | **archetype** | definition, prototype |
| Tree / mine | **resource node** | deposit, gather site |
| Non-interactive decor | **prop** | scenery, static |

## Layers

| Layer | Name | Meaning |
|-------|------|---------|
| Code (ECS) | **entity** / **registry row** | `entt::entity` handle only — not used in player-facing text |
| Simulation | **world object** | Anything that occupies the map or can be targeted |
| Data files | **archetype** | JSON row: stats, art id, `{ "id": "militia", ... }` |
| Runtime | **instance** | One militia on the map spawned from an archetype |

## World object kinds

| Kind | Player term | Examples | Select | Multi-select | Hover ring |
|------|-------------|----------|--------|--------------|------------|
| **unit** | Unit | worker, militia | Yes | Yes | Yes |
| **structure** | Building | town center, barracks | Yes | Yes (AoE2) | Yes |
| **resource node** | Resource | forest patch, gold pile | Yes | No (single tile) | Yes (when selectable) |
| **prop** | — | rocks, decor | No | No | No |

Trees in M1 are **resource node** tiles (forest on `MapGrid`), not separate registry rows yet. Selection stores a **grid cell** for the chosen resource node. The `forest_patch` archetype supplies wood capacity / display metadata; the map still owns per-tile wood amounts.

## Archetype JSON (live)

Shipped under `data/archetypes/` and loaded by `src/data/content_loader.cpp` into `data::ArchetypeDefinition`. Earth civ wires them via `data/civs/earth.json` (the loader opens that path only; no civ discovery loop yet).

| File | Kind | Used for |
|------|------|----------|
| `worker.json` | unit | Gather / deposit |
| `militia.json` | unit | Melee combat |
| `town_center.json` | structure | Stockpile + spawn cost fields |
| `forest_patch.json` | resource_node | Wood capacity metadata |

`ArchetypeDefinition` fields the loader accepts (`content_types.hpp`):

| Field | Notes |
|-------|-------|
| `id`, `kind`, `display_name` | Required identity |
| `max_hp` | Applied at spawn |
| `move_ticks_per_tile` | Movement pacing |
| `gather_per_tick`, `carry_capacity` | Workers |
| `melee_attack` | Damage used by `run_combat_system` today |
| `melee_armor`, `pierce_attack`, `pierce_armor` | Parsed; **not applied** in combat yet |
| `attack_cooldown_ticks` | Melee cadence |
| `spawn_worker_wood_cost` | Structures (TC = 50) |
| `wood_capacity` | Resource nodes; stamped onto forest tiles in `create_test_map` |
| `attack_damage` | Alias accepted for `melee_attack` |

Example (`militia.json`):

```json
{
  "id": "militia",
  "kind": "unit",
  "display_name": "Militia",
  "max_hp": 60,
  "move_ticks_per_tile": 8,
  "melee_attack": 8,
  "melee_armor": 0,
  "pierce_attack": 0,
  "pierce_armor": 0,
  "attack_cooldown_ticks": 12
}
```

Constraints verified in the loader:

- `kind` must be `unit` \| `structure` \| `resource_node` \| `prop`
- Civ manifest lists must resolve to archetypes of the matching kind (`validate_civ_archetypes`)
- Owner is **not** in the JSON row; spawn code assigns `PlayerOwnedTag` / `EnemyTag` (and later player slots)

### JSON is not the whole spawn story

`load_test_scenario` (`src/sim/scenario/test_scenario.cpp`) hard-codes the Earth layout:

| World object | Cell |
|--------------|------|
| Town Center | `(8, 8)` |
| Player worker | `(9, 8)` |
| Player militia | `(10, 8)` |
| Enemy militia | `(45, 45)` |

Player militia also gets a pre-issued `AttackOrder` on the enemy. Adding a new unit type still needs C++ spawn / role wiring (and harness roles if you want command replay). Archetype JSON alone is not enough.

## Naming debt

C++ still mixes taxonomy with older names (`DefinitionRef`, `GatherTarget`, `UnitDefinition`-style comments). Prefer archetype / world object / resource node in new docs and UI strings. Keep `entt::entity` in C++ only.
