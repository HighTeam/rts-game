## Combat / attack pathfinding (deferred)

Epic: gameplay polish — **not blocking M2 exit**. Track here until a dedicated pass.

### Problem

Militia attack approach works when attacker and target are on a **perfect 8-direction alignment** (same row/column or perfect diagonal). From other angles, the unit often:

1. Paths close to the enemy correctly
2. Then takes **two diagonal steps at different angles** (dog-leg) before melee
3. Only then engages

Manual move with **16-direction** pathfinding (incl. knight leaps) is fine for long distance. **Attack** should feel like AoE2: straight grid approach (cardinal + diagonal), no L-shaped final hops.

### What we tried (still insufficient)

| Attempt | Result |
|--------|--------|
| Tighter melee range / contact slide | Hits OK; pathing still bad |
| Stand tile picks “between mover and target” | Better head-on; still dog-legs at angles |
| Attack paths **grid8 only** (no knight leaps) | Better than 16-dir knights; still A* L-shapes |
| **`find_attack_path` Bresenham direct line** + replan if path deviates | User report: issue **still** present near enemy |

### Likely root causes (for next implementer)

1. **A\* fallback** when direct line hits any blocked cell → L-shaped grid8 path for last 2–3 tiles
2. **Every path step ends at tile center** — no sub-tile stand toward enemy on final tile (attack orders have no `goal_world`)
3. **Stand tile vs path goal** may replan each tick; chase keeps non-direct path if goal cell matches
4. **“16-direction” in code ≠ AoE2** — our extra 8 are (2,1) knight jumps; AoE2 combat is 8 grid dirs only

### Acceptance criteria (when picked up)

- [ ] From any angle, militia walks **straight grid line** onto melee stand tile (no dog-leg within 2 chebyshev of stand)
- [ ] Manual move: 16-dir + sub-tile click goal unchanged
- [ ] Attack: 8 grid dirs minimum; optional long-range knight only if it does not affect last 3 tiles to target
- [ ] Harness / lockstep hashes updated; no infinite approach loop at contact

### Code touchpoints

- `src/sim/systems/pathfinding.cpp` — `find_attack_path`, `find_best_melee_stand_tile`, `build_direct_line_path`, `attack_path_follows_direct_line`
- `src/sim/systems/gameplay_systems.cpp` — `run_attack_chase_system`, `begin_move_segment`, `assign_unit_path`
- `src/sim/player/player_commands.cpp` — `issue_attack_order`

### References

- User reports: diagonal double-step before hit; 8-alignment approaches OK
- [docs/DECISIONS.md](../../docs/DECISIONS.md) — sub-tile `Fixed` positions
