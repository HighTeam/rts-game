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

Trees in M1 are **resource node** tiles (forest on `MapGrid`), not separate registry rows yet. Selection stores a **grid cell** for the chosen resource node.

## Archetype JSON shape (future)

```json
{
  "id": "militia",
  "kind": "unit",
  "display_name": "Militia",
  "owner": "player",
  "max_hp": 60,
  "melee_attack": 8,
  "melee_armor": 0,
  "pierce_attack": 0,
  "pierce_armor": 0
}
```

Civ files reference **archetype** ids; **instances** get owner (player 1…8, gaia, neutral) at spawn.

## Code migration (later)

Existing code still uses names like `UnitDefinition`, `GatherTarget`, and `entt::entity`. Rename in a dedicated pass when we expand JSON:

- `data/content_types.hpp` → archetype structs
- Comments / UI strings → world object / resource node / prop
- Keep `entt::entity` in C++ only
