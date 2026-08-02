## [BLOCKING] Deterministic sim core

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M1.

- [x] Tile/grid map representation, single hardcoded test map
- [x] Resource node (wood) + gather/deposit loop for one worker unit
- [x] Town Center equivalent: spawns workers, is a valid attack target
- [x] One military unit: move, attack, die
- [x] A* pathfinding on the grid (don't build flow fields yet — you don't have enough units to need them)
- [x] Per-tick state hash function (hash all sim-relevant component data every tick) — desync detector for M2
