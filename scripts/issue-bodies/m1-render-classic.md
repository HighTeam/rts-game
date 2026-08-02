## Classic render pipeline (DE-style)

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M1.

Reference: [docs/DECISIONS.md](../docs/DECISIONS.md) — render style + Camera view modes.

**Scope now:** Classic camera only (locked isometric, pan + zoom). Full 3D mode is a follow-up epic.

- [x] Camera module: Classic mode — fixed isometric azimuth, pan, smooth zoom (no orbit)
- [x] Grid → world projection (isometric/dimetric mapping from sim grid)
- [ ] Replace top-down colored quads with 3D scene pass (terrain tiles as meshes or extruded quads in 3D)
- [ ] Unit/building draw from ECS (billboards or simple meshes facing camera)
- [ ] Basic shaders: flat/ambient lighting, team-color multiply hook
- [ ] Selection outline shader hook (can draw dummy selection until input exists)
- [ ] `CameraView` setting enum wired internally (`Classic` only; `Full3D` stub disabled in UI until later)
- [ ] Render still read-only over sim registry (see [docs/ECS.md](../docs/ECS.md))

**Out of scope (this epic):** Full 3D orbital camera, art pipeline, asset packing, menu Settings screen (M5 — only documents the Camera view dropdown).
