# Lockstep networking

Runbook for ENet lockstep: 2-player graphical play, reconnect/AI fallback, multi-peer host paths exercised by CI smokes, and known remaining gaps. Same `aoa` binary as singleplayer and the harness.

Verified against `main` after PR **#69** (stacks #59, #64, #67) and the earlier reconnect fixes **#40**, **#42**, **#44**, **#46**, **#48**.

## Intent

Peers run the same deterministic sim. Each peer sends inputs for a future execute tick. Live lockstep does not advance that tick until every required slot has a `TickInputBatch` (empty batches count). After each live tick, peers exchange `state_hash` and stop advancing on mismatch.

M2 closed the 2-player path plus reconnect. M3 started multi-peer gating (`session_player_count` up to 8 internally); graphical host/join is still 2-player. See [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md).

## Roles and slots

| Role | CLI | Player slot |
|------|-----|-------------|
| Host | `--lockstep-host` | `0` (`LOCKSTEP_HOST_PLAYER_SLOT`) |
| Client | `--lockstep-join HOST:PORT` | `1` (`LOCKSTEP_CLIENT_PLAYER_SLOT`) |

Constants live in `src/net/net_constants.hpp`.

| Port constant | Value | Used by |
|---------------|------:|---------|
| `DEFAULT_PORT` | `27000` | Graphical/headless host default; `--net-smoke` |
| `LOCKSTEP_SMOKE_PORT` | `27001` | `--lockstep-smoke` |
| `LOCKSTEP_DISCONNECT_SMOKE_PORT` | `27200` | `--lockstep-disconnect-smoke` |
| `LOCKSTEP_RECONNECT_SMOKE_PORT` | `27201` | `--lockstep-reconnect-smoke` |
| `LOCKSTEP_4_SMOKE_PORT` | `27202` | `--lockstep-4-smoke` |
| `LOCKSTEP_4_DISCONNECT_SMOKE_PORT` | `27203` | `--lockstep-4-disconnect-smoke` |
| `LOCKSTEP_PEER_SILENCE_SMOKE_PORT` | `27204` | `--lockstep-peer-silence-smoke` |

`player_slot` is wire identity, command ownership, and snapshot-key sort order. The default Earth scenario spawns a Town Center, worker, and militia for slot 0 and slot 1. Selection and commands filter by `PlayerSlot`. There is **no** `--player-slot` CLI (`parse_launch_options` in `application.cpp`).

Graphical and headless `--lockstep-host` / `--lockstep-join` build a 2-player session (`LOCKSTEP_PLAYER_COUNT`). `LockstepSession::start_host` accepts `session_player_count_ - 1` clients (one client for graphical host). `--lockstep-4-smoke` / `--lockstep-4-disconnect-smoke` construct sessions with `LOCKSTEP_4_PLAYER_COUNT` (4).

## CLI

Build first ([BUILD.md](BUILD.md)), then:

```powershell
.\build\x64-debug\Debug\aoa.exe --net-smoke
.\build\x64-debug\Debug\aoa.exe --lockstep-smoke
.\build\x64-debug\Debug\aoa.exe --lockstep-disconnect-smoke
.\build\x64-debug\Debug\aoa.exe --lockstep-reconnect-smoke
.\build\x64-debug\Debug\aoa.exe --lockstep-4-smoke
.\build\x64-debug\Debug\aoa.exe --lockstep-4-disconnect-smoke
.\build\x64-debug\Debug\aoa.exe --lockstep-peer-silence-smoke
.\build\x64-debug\Debug\aoa.exe --snapshot-smoke
.\build\x64-debug\Debug\aoa.exe --snapshot-double-spawn-smoke
.\build\x64-debug\Debug\aoa.exe --snapshot-reconnect-smoke
.\build\x64-debug\Debug\aoa.exe --snapshot-heavy-smoke

.\build\x64-debug\Debug\aoa.exe --lockstep-host --port 27000
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000

.\build\x64-debug\Debug\aoa.exe --lockstep-host --headless --ticks 100
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000 --headless --ticks 100
```

| Flag | Port / notes |
|------|----------------|
| `--net-smoke` | `27000` |
| `--lockstep-smoke` | `27001` |
| `--lockstep-disconnect-smoke` | `27200` |
| `--lockstep-reconnect-smoke` | `27201` |
| `--lockstep-4-smoke` | `27202`, 40 ticks (`LOCKSTEP_4_SMOKE_TICKS`) |
| `--lockstep-4-disconnect-smoke` | `27203` |
| `--lockstep-peer-silence-smoke` | `27204`; warmup `35` ticks; wait `LOCKSTEP_PEER_SILENCE_MS + 500` |
| `--snapshot-smoke` (+ double-spawn / reconnect / heavy) | No network |
| `--lockstep-host` / `--lockstep-join HOST:PORT` | Default host port `27000`; optional `--port`, `--headless`, `--ticks`, `--lockstep-debug` |

CI (`.github/workflows/build.yml`) runs: `--headless --ticks 5`, `--net-smoke`, `--lockstep-smoke`, `--lockstep-disconnect-smoke`, `--lockstep-reconnect-smoke`, `--lockstep-4-smoke`, `--lockstep-4-disconnect-smoke`, `--lockstep-peer-silence-smoke`, `--snapshot-smoke`. Harness runs on `ci-x64-debug` only.

`main.cpp` dispatch order: harness → net/lockstep/snapshot smokes → lockstep host/join → headless → graphical. First matching flag wins.

Two-machine checklist: [LAN_SOAK.md](LAN_SOAK.md).

## Architecture

```
GameInput (pick → PlayerCommand)
  → LockstepSession::submit_local_command   // buffer in local_outbox_
  → ensure_local_batch_sent                 // TickInputBatch on reliable channel
  → wait until every required remote slot has a batch
  → flush_local_commands_for_tick           // enqueue_network_command
  → Simulation::tick()
  → TickStateHash (unreliable)
```

| Piece | Location |
|-------|----------|
| Transport | `src/net/enet_transport.*` |
| Wire envelope | `src/net/net_message.*` |
| Batch / hash / latency codec | `src/net/lockstep_wire.*` |
| Reconnect request codec | `src/net/reconnect_wire.*` |
| Snapshot encode/restore | `src/sim/snapshot/sim_snapshot.*` |
| Session | `src/net/lockstep_session.*` |
| Headless host/join + smokes | `src/net/lockstep_runner.*` |
| Graphical loop | `src/app/application.cpp` (`run_graphical_lockstep`) |
| Disconnect AI commands | `src/sim/systems/disconnected_player_ai.*` |

### Graphical tick thread

`run_graphical_lockstep` calls `start_background_tick_loop()`. Sim advance and snapshot publish run on that worker. The SFML thread uses `render_snapshot()`, not a live registry read for picks.

Frame order after #46 (`application.cpp`): `service_network_latency` → `consume_snapshot_restored` (clear selection / reset camera) → capture `input_snapshot` → poll events / continuous input. That avoids using a pre-restore render snapshot for clicks after reconnect.

## Input delay

| Mode | Constant | Value |
|------|----------|------:|
| Singleplayer / harness | `PLAYER_COMMAND_DELAY_TICKS` | `1` |
| Lockstep | `LOCKSTEP_COMMAND_DELAY_TICKS` | `2` |

Separate constants. Lockstep does not stack both.

## Peer targeting (fixed #40)

Host keeps `player_slot_to_enet_slot_` via `bind_player_to_enet_slot` / `enet_slot_for_player` / `clear_enet_slot_mapping` (`lockstep_session.cpp`). Mapping is updated from reconnect requests and inbound batch `sender_slot` (ENet preserves `ReceivedPacket::sender_slot`).

| Message | 2p | Multi-peer (`session_player_count_ > 2`) |
|---------|----|------------------------------------------|
| `JoinAccepted` | `send_reliable` | `send_reliable_to_client(*enet_slot_for_player(target_player_slot))` |
| `ReconnectSnapshot` | `send_reliable` | `send_reliable_to_client(target_enet_slot)` |
| Host own `TickInputBatch` | `send_reliable` | `broadcast_reliable_except(..., nullopt)` |
| Relayed remote `TickInputBatch` | n/a | `broadcast_reliable_except(..., enet_slot_for_player(batch.player_slot))` (except sender) |
| Host `TickStateHash` | `send_unreliable` | `broadcast_unreliable_except(..., nullopt)` (#48) |

**MovePath bounds (#40):** snapshot decode rejects `next_index < 0` or `next_index > cell_count` (`sim_snapshot.cpp`). Movement removes paths when `next_index < 0` or `>= cells.size()` (`gameplay_systems.cpp`).

**Caveat:** if `enet_slot_for_player` is missing, JoinAccepted/snapshot send is skipped or fails; relay `except` becomes `nullopt` and may echo to the sender.

## Resync batch gate (fixed #42)

`should_drop_batch_for_reconnect_handshake(batch_player_slot)`:

- **2p:** drop while `!client_resync_ready_` or `awaiting_reconnect_handshake_` (session-global; only one remote).
- **Multi-peer:** drop only batches from `pending_reconnect_player_slot_`; other slots keep flowing.

`is_valid_remote_player_slot` rejects out-of-range / self slots before indexing ready-bit maps (batches and hashes).

`resync_strict_batch_gate_` still tightens accepted execute-tick windows for one tick after resync; cleared after the next live advance.

## Duplicate ResyncReady + ManualControlTag (fixed #44)

- Host `handle_resync_ready`: if `client_resync_ready_ && !awaiting_reconnect_handshake_`, logs `resync_ready_duplicate_ignored` and returns (no second `reset_tick_sync_state`).
- Client `maybe_retry_resync_ready` only retries while `awaiting_reconnect_handshake_` is still true (stops after bootstrap clears it).
- `Simulation::set_player_ai_controlled(slot, true)` removes `ManualControlTag` only for workers whose `PlayerSlot` matches that slot. AI command generation also skips `ManualControlTag` workers (`disconnected_player_ai.cpp`).

## Stale render snapshot (fixed #46)

See Graphical tick thread above. Restore frame clears selection before input uses the new snapshot.

## Host TickStateHash fan-out (fixed #48; relay still missing)

For `session_player_count_ > 2`, host `send_state_hash` uses `broadcast_unreliable_except(..., nullopt)` so every client gets the host hash.

Clients still `send_unreliable` only to the host. Host does **not** relay client hashes to other clients. Client-side `verify_state_hash` waits on `required_remote_slots_mask()`; missing peer hashes mean verification never completes on clients (no false desync, but no client↔client wire check). Host-side comparison of all client hashes works once those hashes arrive.

## Multi-peer AI fallback (fixed #59; 2p path kept)

| Path | Behavior |
|------|----------|
| 2p | `note_opponent_transport_down` → `enter_ai_fallback()` sets global `ai_fallback_`, solo-advances via `try_advance_ai_fallback_tick` |
| Multi-peer | `note_player_slot_transport_down(slot)` → `enable_slot_ai_control(slot)` + `ai_controlled_slots_mask_`; remaining peers stay in live lockstep |
| Peer loss | `consume_peer_lost_slot` maps ENet slot → player slot, clears mapping, then per-slot AI |
| AI batches | Host `prepare_ai_controlled_slots_for_tick` builds AI commands, `send_slot_input_batch` broadcasts them, defers host apply via `pending_ai_input_batches_` until `flush_pending_ai_input_batches` |

Regression: `--lockstep-4-disconnect-smoke` (port `27203`).

## Host local outbox (fixed #64)

`handle_peer_connected` and `handle_resync_ready` call `flush_pending_local_commands_to_input_log()` **before** `reset_tick_sync_state()` (which clears `local_outbox_`).

Mid-match `handle_reconnect_request`: when `client_resync_ready_` is already true and reconnect is still needed, sets `opponent_needs_snapshot_ = true` and clears `client_resync_ready_` so a retrying client gets a fresh snapshot (re-arm).

**Caveat:** `enter_ai_fallback` and `handle_reconnect_snapshot` still call `reset_tick_sync_state()` without that flush. AI takeover / client-side restore can still drop unflushed outbox entries on those paths.

## Silence-based AI (#67 intent; grace ordering caveat)

`is_opponent_unresponsive()` (`lockstep_session.cpp`): host, match started, remote batch seen, not (2p + `ai_fallback_`), hash warmup done, waiting on a remote batch longer than `LOCKSTEP_PEER_SILENCE_MS` (3000). Grace active only suppresses silence while `resync_strict_batch_gate_` or `awaiting_reconnect_handshake_`.

2p silence / disconnect path: `note_player_slot_transport_down` sets `opponent_reconnect_pending_`, calls `begin_opponent_reconnect_grace()`, then `enter_ai_fallback()`.

`enter_ai_fallback` disconnects a still-connected 2p peer when:

```text
role == Host && transport_.is_connected()
  && !is_opponent_reconnect_grace_active()
  && session_player_count_ == 2
```

**Verified caveat:** the 2p transport-down path arms reconnect grace *before* `enter_ai_fallback`, so the grace guard still skips `disconnect_peer()` on that call. `#67` removed the old `!opponent_reconnect_pending_` guard but left the grace guard. Smoke `--lockstep-peer-silence-smoke` (port `27204`) asserts host AI + host disconnect + client reconnect; treat CI results for that smoke as the runtime check of whether another path eventually drops the peer.

## Reconnect flow

Initial join (before match ticks): client `ReconnectRequest` → host `JoinAccepted`.

Mid-match: host `ReconnectSnapshot` → client apply → `ResyncReady` → host resumes that slot. `JoinAccepted` is not part of the normal mid-match snapshot path.

### Snapshot blob

Magic `0x414F4153` (`AOAS`), `SNAPSHOT_VERSION = 9` (`sim_snapshot.cpp`).

Public APIs (`sim_snapshot.hpp`):

| API | Role |
|-----|------|
| `encode_sim_snapshot` | Serialize checkpoint |
| `apply_sim_snapshot` | Restore into a `Simulation` |
| `decode_sim_snapshot_metadata` | Metadata without mutating a live sim |
| `validate_snapshot_input_replay` | Replay-only hash check |
| `diagnose_snapshot_roundtrip_failure` | stderr diagnosis |

`apply_sim_snapshot` resets via `load_test_scenario`, then overlays map/fog/entities. It does **not** rebuild the world by replaying `input_log`; the log is restored into `CommandQueue` only. Entity ids are not stable across restore; use `EntitySnapshotKey`.

Host `send_reconnect_snapshot`: flush pending local commands → encode → roundtrip validate → self-apply → send.

## Wire messages

| Kind | Value | Channel |
|------|------:|---------|
| `PlayerCommand` | 1 | reliable (`--net-smoke`) |
| `TickStateHash` | 2 | unreliable |
| `TickInputBatch` | 3 | reliable |
| `ReconnectRequest` | 4 | reliable |
| `ReconnectSnapshot` | 5 | reliable |
| `JoinAccepted` | 6 | reliable |
| `ResyncReady` | 7 | reliable |
| `LatencyProbe` | 8 | unreliable |
| `LatencyPong` | 9 | unreliable |

`CHANNEL_RELIABLE = 0`, `CHANNEL_UNRELIABLE = 1`.

## Network HUD

`PING: Nms` from `LockstepSession::network_hud_stats()` → `hud_overlay.cpp`.

| Field | Behavior |
|-------|----------|
| `active` | Connected and not AI fallback |
| `local_ping_ms` | Min of last `LOCKSTEP_PING_DISPLAY_WINDOW` (8) samples; else latest; else smoothed RTT |
| `peer_latency_ms` | Struct field only; **not filled** |

Probes every `LOCKSTEP_LATENCY_PROBE_INTERVAL_MS` (200). Samples `<= 0` or `> LOCKSTEP_RTT_SAMPLE_MAX_MS` (100) dropped.

## Combat sort key

`run_combat_system` / `run_attack_chase_system` sort attackers with `snapshot::sort_entities_by_snapshot_key` (`EntitySnapshotKey`: slot + category + ordinal), not raw `entt::entity` id (`gameplay_systems.cpp`).

## Runtime data paths

Compile definition `AOA_RUNTIME_ROOT="${CMAKE_SOURCE_DIR}"` (`CMakeLists.txt`). There is **no** `AOA_DATA_DIR` env var in code.

`default_data_directory()` (`runtime_paths.cpp`) searches: exe/`data` (Windows) → `AOA_RUNTIME_ROOT`/`data` → cwd `data`. POST_BUILD copies `data/` and `assets/` next to the exe. Non-Windows `executable_directory()` returns empty.

## Remaining gaps / pitfalls

| Item | Status in code |
|------|----------------|
| Graphical host >1 client / `--player-slot` | Not implemented; still 2p CLI slots 0/1 |
| `--lockstep-8-smoke` | Not present |
| Client↔client `TickStateHash` relay | Missing; host fan-out only (#48) |
| Host migration | Policy only ([DECISIONS.md](DECISIONS.md) / [BACKLOG.md](BACKLOG.md)) |
| Silence → `disconnect_peer` while connected | Intended in `enter_ai_fallback`; grace armed first on 2p transport-down may still skip drop (see above) |
| `enter_ai_fallback` / client snapshot restore outbox flush | Still clear `local_outbox_` without flush |
| `peer_latency_ms` HUD | Unused |
| HARNESS.md `AOA_DATA_DIR` wording | Stale vs `AOA_RUNTIME_ROOT` |

## Related

- [BUILD.md](BUILD.md) — presets and short CLI table
- [HARNESS.md](HARNESS.md) — scenario hashes and command replay
- [LAN_SOAK.md](LAN_SOAK.md) — two-PC soak checklist
- [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md) — 4–8 player scale plan
- [ECS.md](ECS.md) — tick pipeline after commands apply
- [DECISIONS.md](DECISIONS.md) — command timing, disconnect policy
