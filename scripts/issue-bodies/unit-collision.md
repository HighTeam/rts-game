## Unit collision / overlap (deferred)

Epic: gameplay polish — **not M2 blocking**.

### Problem

Units (e.g. workers, militia) can **walk through each other** instead of blocking or pushing aside. Multiple units can occupy the same tile or pass through each other while moving.

### Desired behavior (AoE2-style baseline)

- Units block each other's path tiles during movement
- No two friendly units share the same stand tile at rest (or use soft push / separation)
- Pathfinding respects occupied cells (or dynamic replan when blocked)

### Code touchpoints

- `src/sim/systems/pathfinding.cpp` — occupancy / blocked cells
- `src/sim/systems/gameplay_systems.cpp` — movement, contact
- `src/sim/components/movement.hpp` — path segments, world position

### Acceptance criteria

- [ ] Two workers ordered to the same tile do not stack; second waits or picks adjacent stand
- [ ] Moving unit cannot pass through another unit's cell
- [ ] Harness / lockstep hashes updated if sim behavior changes
