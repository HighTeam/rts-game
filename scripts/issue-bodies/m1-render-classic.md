## Classic render pipeline (DE-style)

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M1.

Reference: [docs/DECISIONS.md](../docs/DECISIONS.md) — render style + Camera view modes.

**Scope now:** Classic camera only (locked isometric, pan + zoom). Full 3D mode is a follow-up epic.

- [x] Camera module: Classic mode — fixed isometric azimuth, pan, smooth zoom (no orbit)
- [x] Grid → world projection (isometric/dimetric mapping from sim grid)
- [x] Replace flat quads with 3D scene pass (extruded terrain + entity prisms, depth buffer)
- [x] Unit/building draw from ECS (colored prisms with team-color hook)
- [x] Basic shaders: per-vertex lit colors (ambient + darker sides)
- [x] Selection outline hook (gold ring on Town Center until input exists)
- [x] `CameraView` setting wired internally (`Classic` active; `Full3D` stub unavailable)
- [x] Render still read-only over sim registry (see [docs/ECS.md](../docs/ECS.md))

**Out of scope (this epic):** Full 3D orbital camera, art pipeline, asset packing, menu Settings screen (M5 — only documents the Camera view dropdown).

**Follow-up (post-M1 / M2 prep):** sub-tile `Fixed` movement + render interpolation; real art/meshes; Full 3D camera mode.
