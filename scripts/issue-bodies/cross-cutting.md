## Cross-cutting early decisions

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md). Decide once — expensive to change later.

- [x] Fixed-point math (M0) — expensive to retrofit
- [x] Data-driven civ/unit/building definitions (M1) — expensive to retrofit
- [x] Host migration policy (M3) — **no migration**; clients exit if host is gone
- [x] Disconnect/pause policy (M2 prep) — AI takeover on disconnect; player resumes on reconnect — [docs/DECISIONS.md](../docs/DECISIONS.md)
- [x] Tick rate — pick once; changing later touches balance, netcode timing, and input feel (`SIM_TICKS_PER_SECOND = 20`)
- [x] Render style — AoE2 DE-like hybrid (3D engine, isometric Classic camera, zoom + shaders) — [docs/DECISIONS.md](../docs/DECISIONS.md)
- [x] Camera view modes — Settings: **Classic** (now) vs **Full 3D** (later default)
- [x] Combat same-tick resolution — sorted entity id; no double-KO same tick — [docs/DECISIONS.md](../docs/DECISIONS.md)
- [x] Unit position model — grid cells in M1; sub-tile `Fixed` before M2 — [docs/DECISIONS.md](../docs/DECISIONS.md)
