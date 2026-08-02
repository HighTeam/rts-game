## Data-driven civ definitions [BLOCKING for M4]

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M1.

- [x] Civ/unit/building stats loaded from JSON, not hardcoded in C++ classes
- [x] Highest-leverage task: if clean now, M4 is content authoring; if skipped, M4 becomes a refactor under time pressure

Earth civ definition: `data/civs/earth.json`. Loaded via `src/data/content_loader.cpp` at scenario setup.
