# Age of Affinities — Product Backlog

How to use this: each `## Pillar` = a GitHub Milestone. Each `###` = an Issue (Epic) with a checkbox body — GitHub tracks % complete. Split any item into its own Issue if it grows past ~1 sitting. `[BLOCKING]` = nothing downstream in that pillar (or the next) should start until it closes.

**Version bumps** are owner-gated: do not raise game version unless told. Bumps may be `+0.0.1`, `+0.1.0`, etc., depending on the change set. Versioning exists so mismatched clients cannot share a lobby.

Suggested labels: `engine`, `netcode`, `content`, `ui`, `tooling`, `blocking`, `epic`.

---

## Status snapshot (keep current)

| Area | State |
|------|--------|
| Foundations (window, fixed-point, ECS, CMake/vcpkg) | Done |
| Earth core sim (worker, militia, TC, gather/deposit, pathfinding, hash) | Done |
| Classic isometric render + fog + HUD basics | Done |
| Lockstep 2–4p, reconnect, AI takeover on disconnect | Done |
| SP save/load + autosave; House / Lumberjack / Extractor / Mana lake | Done (polish remains) |
| Crossing / Commons + Pattern Maker; lobby map picker | Shipped (size/fairness polish below) |
| 8-player brutal soak, MP coordinated save/load | Open |
| Map redesign → RMG → Ages/Civs | Planned (this backlog) |

---

## Pillar: Polish & Debt

*Goal: stabilize the current Earth loop before large content/map pivots.*

### Open netcode / persistence

- [ ] Multiplayer save/load — coordinated multi-peer load (encode already has snapshot + input log)
- [ ] LAN brutal soak — 8 players (deferred from M3; omit from “must ship now” unless needed)

### Open gameplay / engine debt

- [ ] **Combat attack pathfinding** — dog-leg / double-diagonal near enemy when not 8-aligned; see [scripts/issue-bodies/pathfinding-combat.md](../scripts/issue-bodies/pathfinding-combat.md)
- [ ] **Unit collision** — harden unit–unit blocking (soft-slide still leaks); see [scripts/issue-bodies/unit-collision.md](../scripts/issue-bodies/unit-collision.md)
- [ ] Extractor / Mana lake sprite offsets — visual pass until footprint and art agree
- [ ] Work interact / stand range — keep tuned (currently `WORK_INTERACT_RANGE_TILES = 0.8`)
- [ ] Hitbox / movement — units must not walk center through 1×1 resource tiles or buildings mid-segment

### Earth content already landed (reference, not open work)

- [x] House (+3 civil cap)
- [x] Lumberjack (wood drop-off)
- [x] Mana lake (Nature, animated) + Extractor (build-on-lake, +max mana, timed extract)
- [x] Dynamic mana cap (start 0/0; no TC mana pump)
- [x] Multi-select move formation goals; building spawn order S→N; place-mode T:Deselect / RMB cancel

---

## Pillar: Map Redesign

*Goal: keep the isometric grid, expand the world language. **Must land before Ages and Civs.***

### [BLOCKING] Tile & nature vocabulary

- [ ] New ground tiles (e.g. sand, snow) with walk/path rules
- [ ] New tree / forest variants and harvest rules where applicable
- [ ] New obstacles (blocking, non-harvest or special)
- [ ] Nature undestructable “buildings” / props (landmarks) — selectable info, no player destroy
- [ ] Data-driven tile/prop archetypes (JSON) — avoid hardcoding every new tile in C++

### [BLOCKING] Map layering

- [ ] Height / layer model for hills and elevated obstacles (grid stays isometric; clarify sim vs render ownership)
- [ ] Movement / pathfinding rules for slopes, cliffs, blocked facings
- [ ] Placement rules for buildings on layered terrain
- [ ] Fog / vision interaction with layers (decide once, document in DECISIONS.md)

### Scenario / hand-authored maps

- [ ] Refresh test / skirmish layouts using the new tile set
- [ ] Fairness checklist for hand maps (symmetric or intentional asymmetry)

---

## Pillar: RMG & Fairness

*Goal: generate playable maps from a pattern/script after the new map vocabulary exists.*

### [BLOCKING] Pattern / script pipeline

- [x] Map pattern format (current `.pattern` + Pattern Maker) — tile vocabulary still expands with Map Redesign
- [x] Generator reads pattern → places bases, resources, roads (Crossing hardcoded; Commons + Other… files)
- [x] Deterministic seed + lockstep-safe generation (same seed ⇒ same map on all peers)

### Accepted current gen (polish later)

Picker is Crossing / Commons / Other…. Commons is the default and is unlocked for 48 / 64 / 96 / 128. Crossing stays 64×64 and 2-player locked. Default / Continental / Archipelago are out of the picker (old `builtin` strings still parse).

- [ ] Commons pieces are authored in 96-space; changing lobby size does not scale offsets — 48/64 clamp, 128 stays sparse
- [ ] Per-size fairness pass (start rings, forest / gold / berry density, chokes)
- [ ] Crossing multi-size or dedicated 2p-only copy if we want more than 64×64

### Fairness & validation

- [ ] Resource / start balance checks (distance rings, choke fairness)
- [ ] Reject or repair unfair rolls
- [ ] Dev tools: dump seed, preview map, regenerate

---

## Pillar: Versioning & Lobby Compatibility

*Goal: players only match with compatible builds. Owner controls when the number increases.*

### Version identity

- [ ] Single source of truth for game version (build + runtime readable)
- [ ] Show version in main menu / lobby UI
- [ ] **Do not bump version unless the owner says so** (may be `+0.0.1` or larger in one drop)

### Lobby enforcement

- [ ] Host advertises required/protocol version (or exact version policy)
- [ ] Joiners with mismatched version are rejected with a clear message
- [ ] Example policy: peers on `1.4.5` can play together; `1.4.6` cannot join a `1.4.5` lobby (exact match unless a wider “compatible range” is decided later)
- [ ] Document wire field(s) in DECISIONS.md / net docs

---

## Pillar: Ages & Technologies

*Goal: four ages with unique tech identity. Starts after Map Redesign is far enough that age content has a world to live in.*

### Ages

- [ ] **Age of Human**
- [ ] **Age of Magic**
- [ ] **Age of Technology**
- [ ] **Age of Spirits**
- [ ] Age-up flow (costs, requirements, UI hooks) — data-driven

### Technologies

- [ ] Per-age unique technology sets in JSON
- [ ] Generic bonus hook system (“modify stat X by Y under condition Z”) — design once, reuse
- [ ] Research buildings / UI stubs (full GUI may wait on GUI Redesign)

---

## Pillar: Civilizations & Generals

*Goal: four civs with AoM-style major/minor leadership (generals). After map + age foundations.*

### Civilizations (×4)

- [ ] Unique unit roster per civ (data files)
- [ ] Civ bonuses via the shared hook system
- [ ] Balance pass (timeboxed)

### Generals (AoM-style)

- [ ] 1–3+ generals per civ (major + minor style)
- [ ] General pick / unlock rules
- [ ] Per-general unique units, techs, or powers (data-driven)
- [ ] Lobby / game-setup selection hooks

---

## Pillar: Art & Presentation

*Goal: raise visual fidelity without blocking sim pillars.*

### Map presentation

- [ ] Map / terrain shaders (lighting, biomes, height cues)
- [ ] Tile/prop art pass for new sand/snow/trees/obstacles

### Units & buildings

- [ ] Unit models (replace placeholders where needed)
- [ ] Building models
- [ ] Animation pipeline conventions (spritesheets vs meshes — decide and document)

---

## Pillar: GUI Redesign

*Goal: replace / overhaul HUD and menus from the paper reference.*

- [ ] Capture paper reference into repo notes or design pack (screens, panels, flow)
- [ ] Information architecture: options panel, info panel, minimap, resource bar, chat
- [ ] In-game HUD redesign pass
- [ ] Menu shell redesign (ties to Shell & Packaging)
- [ ] Build / train / research panel patterns for ages & generals

---

## Pillar: Shell & Packaging

*Goal: shippable client shell around the working game.*

### Main menu & lobby

- [ ] Singleplayer / Multiplayer / Settings / Account / Credits / Leave
- [ ] Logo + menu art
- [ ] Map selection, resources, unit cap, map size
- [ ] Civ / general / age-related setup as content unlocks
- [ ] Ready-check; **version gate** (see Versioning pillar)

### Settings & AI

- [ ] Video, Audio, Controls, Account (nickname MVP)
- [ ] Rule-based AI opponent (no ML)

### Packaging

- [ ] Asset pack pipeline maturity (`assets.dat` / future packs)
- [ ] Windows installer / distribution package
- [ ] Minimal download page (do not over-invest for MVP)

### Analytics (optional / later)

- [ ] Define metrics before building pipes (APM, resources, duration, …)

---

## Suggested order of attack

1. **Polish & Debt** — enough that the Earth build is trustworthy  
2. **Map Redesign** — tiles, layering, nature  
3. **RMG & Fairness** — pattern/script generation on the new vocabulary  
4. **Versioning** — as soon as multiplayer lobbies are used for wider playtests (can overlap earlier)  
5. **Ages & Technologies** then **Civilizations & Generals**  
6. **Art & Presentation** and **GUI Redesign** in parallel where staffed  
7. **Shell & Packaging** for external playtests / release  

If schedule slips: cut tech-tree depth and general count before cutting map fairness, lockstep robustness, or data-driven architecture.
