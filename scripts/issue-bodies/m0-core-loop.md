## [BLOCKING] Core loop skeleton

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M0.

- [x] Fixed-timestep sim loop decoupled from render loop (pick a tick rate — 20/sec is a reasonable AoE2-era default — and write this so render can interpolate between ticks later)
- [x] Window + OpenGL context via SFML, clear-color triangle to confirm the pipeline
- [x] Fixed-point math library (Q16.16 or similar) — do this now, not when you "need" it; retrofitting fixed-point into float-based sim code later is a rewrite, not a patch
- [x] Headless build mode (sim runs with no window) — needed for automated desync/regression testing later, cheap to add now
