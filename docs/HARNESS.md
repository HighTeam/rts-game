# Headless regression harness

Runbook for deterministic sim checks. The same `aoa` binary drives graphical play, ad-hoc headless ticks, and scripted scenarios.

## Intent

Catch sim regressions (movement, gather, combat, command timing) before they become multiplayer desyncs. Scenarios load the default Earth test setup, optionally replay tick-scoped player commands, then assert the final FNV-1a `state_hash`.

## CLI

Build first (see [BUILD.md](BUILD.md)), then from a tree where `data/` resolves (CI and local presets define `AOA_DATA_DIR`):

```powershell
# All scenarios under data/scenarios/*.json
.\build\x64-debug\Debug\aoa.exe --harness

# Ad-hoc: default Earth scenario, no scripted commands
.\build\x64-debug\Debug\aoa.exe --headless --ticks 200 --print-hash
.\build\x64-debug\Debug\aoa.exe --headless --ticks 200 --expect-hash 0x7982a0643f3bcb76
```

| Flag | Effect |
|------|--------|
| `--harness` | Load every `data/scenarios/*.json`, run each, exit non-zero on any mismatch |
| `--headless` | Run sim with no window |
| `--ticks N` | Headless tick count (default `HEADLESS_DEFAULT_TICK_COUNT` = 100) |
| `--print-hash` | Print `state_hash=0x…` after headless run |
| `--expect-hash HEX` | Fail headless if hash differs (`0x` prefix optional) |

`--harness` and `--headless` are separate modes. Harness ignores `--ticks` / `--expect-hash`; those belong on the scenario JSON.

Example hashes above match `data/scenarios/*.json` on this tree. After intentional sim changes, refresh them with `--print-hash` / `--harness` output; do not copy older docs blindly.

## Scenario JSON

Files live in `data/scenarios/`. Minimal shape:

```json
{
  "scenario_id": "earth_default",
  "ticks": 200,
  "expected_state_hash": "0x7982a0643f3bcb76"
}
```

Optional `commands` array (command replay):

```json
{
  "scenario_id": "earth_player_commands",
  "ticks": 150,
  "expected_state_hash": "0x4e8703cf1988f556",
  "commands": [
    {
      "execute_tick": 5,
      "type": "gather",
      "units": ["player_worker"],
      "cell": { "x": 20, "y": 12 }
    },
    {
      "execute_tick": 100,
      "type": "deposit",
      "units": ["player_worker"]
    }
  ]
}
```

| Field | Notes |
|-------|-------|
| `scenario_id` | Must be a **supported** id (see below). Selects which setup the runner accepts. |
| `ticks` | How many `Simulation::tick()` calls to run |
| `expected_state_hash` | Hex string, `0x` optional |
| `commands[].execute_tick` | Applied when `tick_count_` reaches this value (same timing as live play) |
| `commands[].type` | `move` \| `attack` \| `gather` \| `deposit` \| `spawn_worker` |
| `commands[].units` | Scenario **roles**, not archetype ids |
| `commands[].cell` | Required for `move` / `gather` |
| `commands[].target` | Role name for `attack`; town center entity for `spawn_worker` |

### Supported scenario ids

Hard-coded in `run_scenario()` today:

- `earth_default` — default Earth spawn; hard-coded `AttackOrder` pairs drive combat chase; no scripted commands. Slot-1 militia is `PlayerOwnedTag` only (no `EnemyTag`), so `run_enemy_militia_ai` does not run on this setup.
- `earth_player_commands` — same spawn; replay gather/deposit via `PlayerCommand`

Adding a new id requires a code change in `src/harness/regression_harness.cpp` until the harness maps ids to loaders generically.

### Scenario roles

Resolved by `sim::scenario::find_scenario_entity` (slot-aware):

| Role | Lookup |
|------|--------|
| `player_worker` | `WorkerUnitTag` + `PlayerOwnedTag`, slot 0 |
| `player2_worker` | `WorkerUnitTag` + `PlayerOwnedTag`, slot 1 |
| `player_militia` | `MilitiaUnitTag` + `PlayerOwnedTag`, slot 0 |
| `player2_militia` / `enemy_militia` | `MilitiaUnitTag` + `PlayerOwnedTag`, slot 1 |
| `town_center` / `player1_town_center` | `TownCenterTag` + `PlayerOwnedTag`, slot 0 |
| `player2_town_center` | `TownCenterTag` + `PlayerOwnedTag`, slot 1 |

## How a harness tick works

For each tick index `0 .. ticks-1`:

1. Enqueue every scenario command whose `execute_tick` equals `simulation.next_command_execute_tick()` (currently `tick_count + 1`).
2. Call `simulation.tick()`, which applies due commands, runs gameplay systems, then updates `state_hash`.

So a command with `"execute_tick": 5` is enqueued when `tick_count_ == 4` and applied at the start of tick 5 — same delay as `PLAYER_COMMAND_DELAY_TICKS`.

Codepaths: `src/harness/regression_harness.cpp`, `src/sim/player/command_queue.cpp`, `src/sim/simulation.cpp`.

## Updating an expected hash

After an **intentional** sim change:

1. Run `--harness` (or `--headless --ticks N --print-hash` for the default setup).
2. Copy the printed `hash=0x…` into the scenario’s `expected_state_hash`.
3. Re-run `--harness` and commit the JSON with the sim change.

Never “fix” a hash without understanding the gameplay delta. Hash churn is a signal.

## What the hash covers

`compute_state_hash` mixes map tiles / forest wood, fog explored/memory planes (not the per-tick `visible` plane), then every non-world entity with `GridPosition`, sorted by stable snapshot key (player slot, category, ordinal). Per entity it includes (when present): grid + world position, health, carried wood, stockpile, attack order target key, attack cooldown, gather target, worker brain state, `ManualControlTag`, move path, move segment.

Render-only state is excluded. See `src/sim/systems/gameplay_systems.cpp`. Fog layout: [ECS.md](ECS.md).

## Common pitfalls

| Symptom | Likely cause |
|---------|----------------|
| Hash mismatch after a “render only” change | Accidental sim edit, component default, or spawn order |
| Unknown scenario unit role | Typo in `units` / `target`; role must match the table above |
| Unsupported `scenario_id` | New JSON id without harness allow-list update |
| Command seems one tick late/early | `execute_tick` is absolute sim tick, not “ticks from now” |
| Player militia won’t auto-attack in replay | By design — player militia auto-AI was removed; issue an `attack` command |
| Scenarios directory not found | Binary run without `AOA_DATA_DIR` / cwd that reaches `data/` |

## Related

- [BUILD.md](BUILD.md) — presets and daily build commands
- [LOCKSTEP.md](LOCKSTEP.md) — multiplayer smokes, reconnect, desync runbook
- [ECS.md](ECS.md) — tick pipeline and system order
- [DECISIONS.md](DECISIONS.md) — command delay, combat tie-break, disconnect policy
