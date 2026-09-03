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

Shipped under `data/archetypes/` and loaded by `src/data/content_loader.cpp` into `data::ArchetypeDefinition`. Earth civ wires them via `data/civs/earth.json` (`unit_archetypes`, `structure_archetypes`, `resource_node_archetypes`).

| File | Kind | Used for |
|------|------|----------|
| `worker.json` | unit | Gather / deposit / build |
| `militia.json` | unit | Melee combat |
| `mage.json` | unit | Ranged / mage combat |
| `town_center.json` | structure | Stockpile, train, age-up |
| `house.json` | structure | Civil pop cap |
| `barracks.json` | structure | Militia training |
| `mage_academy.json` | structure | Mage training |
| `lumber_camp.json` | structure | Wood drop-off |
| `mill.json` | structure | Food drop-off / farm unlock |
| `mining_camp.json` | structure | Gold drop-off |
| `market.json` | structure | Trade |
| `tower.json` | structure | Defense |
| `farm.json` | structure | Food production |
| `garden.json` | structure | Earth support building |
| `extractor.json` | structure | Build on mana lake; timed extract |
| `mana_lake.json` | structure | Mana lake (build-on target for Extractor) |
| `reservoir.json` | structure | Mana reservoir |
| `forest_patch.json` | resource_node | Wood capacity metadata (map owns per-tile wood) |
| `berry_bush.json` | resource_node | Food bush |
| `blueberry_bush.json` | resource_node | Food bush variant |
| `gold_mine.json` | resource_node | Gold mine |

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
- Civ manifest lists must resolve to archetypes of the matching kind
- Owner is **not** in the JSON row; spawn code assigns `PlayerOwnedTag` / `EnemyTag` (and later player slots)

## Naming debt

C++ still mixes taxonomy with older names (`DefinitionRef`, `GatherTarget`, `UnitDefinition`-style comments). Prefer archetype / world object / resource node in new docs and UI strings. Keep `entt::entity` in C++ only.
