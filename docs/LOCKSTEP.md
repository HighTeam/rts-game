# Lockstep networking

Runbook for ENet lockstep: 2–8 player sessions, reconnect / AI fallback, and CI smokes.
Same `AgeofAffinities` binary as singleplayer and the harness.

Verified against `main` @ `alpha_v0.2.1` (`bfc6a29`): M2 reconnect stack (#40–#48, #59, #64, #67),
M3 multi-peer CLI (`--players` / `--player-slot`), `--lockstep-4-reconnect-smoke`, plus resign /
`HostEnded`, snapshot v26 (map pings), and second-reconnect handshake polish.

## Intent

Peers run the same deterministic sim. Each peer sends inputs for a future execute tick.
Live lockstep does not advance that tick until every required slot has a `TickInputBatch`
(empty batches count). After each live tick, peers exchange `state_hash` and stop on mismatch.

## Roles, slots, and ports

| Role | CLI | Default slot |
|------|-----|--------------|
| Host | `--lockstep-host` | `0` |
| Client | `--lockstep-join HOST:PORT` | `1`, or `--player-slot N` (2–8) |

`--players N` / `--lockstep-players N` (2, 4, or 8) must match on host and every join.
Host waits until the lobby is full before the sim advances.

Constants: `src/net/net_constants.hpp`.

| Port constant | Value | Used by |
|---------------|------:|---------|
| `DEFAULT_PORT` | `27000` | Host default; `--net-smoke` |
| `LOCKSTEP_SMOKE_PORT` | `27001` | `--lockstep-smoke` |
| `LOCKSTEP_DISCONNECT_SMOKE_PORT` | `27200` | `--lockstep-disconnect-smoke` |
| `LOCKSTEP_RECONNECT_SMOKE_PORT` | `27201` | `--lockstep-reconnect-smoke` |
| `LOCKSTEP_4_SMOKE_PORT` | `27202` | `--lockstep-4-smoke` |
| `LOCKSTEP_4_DISCONNECT_SMOKE_PORT` | `27203` | `--lockstep-4-disconnect-smoke` |
| `LOCKSTEP_PEER_SILENCE_SMOKE_PORT` | `27204` | `--lockstep-peer-silence-smoke` |
| `LOCKSTEP_2H2AI_SMOKE_PORT` | `27205` | `--lockstep-2h2ai-smoke` |
| `LOCKSTEP_4_STRESS_PORT` | `27206` | `--lockstep-4-stress-smoke` |
| `LOCKSTEP_4_RECONNECT_SMOKE_PORT` | `27207` | `--lockstep-4-reconnect-smoke` |

`player_slot` is wire identity, command ownership, and snapshot-key sort order.

## CLI

Build first ([BUILD.md](BUILD.md)), then:

```powershell
$exe = ".\build\x64-debug\Debug\AgeofAffinities.exe"

& $exe --net-smoke
& $exe --lockstep-smoke
& $exe --lockstep-disconnect-smoke
& $exe --lockstep-reconnect-smoke
& $exe --lockstep-4-smoke
& $exe --lockstep-4-disconnect-smoke
& $exe --lockstep-peer-silence-smoke
& $exe --lockstep-4-reconnect-smoke
& $exe --snapshot-smoke

# 2p graphical
& $exe --lockstep-host --port 27000
& $exe --lockstep-join 127.0.0.1:27000

# 4p multi-process (connect slots in order 2→3→4)
& $exe --lockstep-host --port 27000 --players 4
& $exe --lockstep-join 127.0.0.1:27000 --players 4 --player-slot 2 --headless
```

CI (`.github/workflows/build.yml`) runs headless ticks plus net/lockstep/4-peer/silence/4-reconnect
and snapshot smokes on both CI presets; `--harness` on `ci-x64-debug` only.

`main.cpp` dispatch: harness → net/lockstep/snapshot smokes → lockstep host/join → headless → graphical.
First matching flag wins.

Two-machine checklist: [LAN_SOAK.md](LAN_SOAK.md). Scale layouts: [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md).

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
| Graphical match | `src/app/application.cpp` (`run_lockstep_match`) |
| Disconnect AI commands | `src/sim/systems/disconnected_player_ai.*` |

### Graphical tick thread

Sim advance and snapshot publish run on a background tick loop. The SFML thread uses
`render_snapshot()`, not a live registry read for picks.

Frame order after #46: `service_network_latency` → `consume_snapshot_restored`
(clear selection / reset camera) → capture input snapshot → poll events.
That avoids using a pre-restore render snapshot for clicks after reconnect.

## Input delay

| Mode | Constant | Value |
|------|----------|------:|
| Singleplayer / harness | `PLAYER_COMMAND_DELAY_TICKS` | `1` |
| Lockstep | `LOCKSTEP_COMMAND_DELAY_TICKS` | `2` |

Separate constants. Lockstep does not stack both.

## Peer targeting (fixed #40)

Host keeps `player_slot_to_enet_slot_` via `bind_player_to_enet_slot` /
`enet_slot_for_player` / `clear_enet_slot_mapping`.

| Message | 2p | Multi-peer (`session_player_count_ > 2`) |
|---------|----|------------------------------------------|
| `JoinAccepted` | `send_reliable` | `send_reliable_to_client` for target player |
| `ReconnectSnapshot` | `send_reliable` | targeted `send_reliable_to_client` |
| Host `TickInputBatch` | `send_reliable` | `broadcast_reliable_except(..., nullopt)` |
| Relayed remote batch | n/a | broadcast except sender |
| Host `TickStateHash` | `send_unreliable` | `broadcast_unreliable_except(..., nullopt)` (#48) |

**MovePath bounds (#40):** snapshot decode rejects invalid `next_index`; movement clears bad paths.

## Resync batch gate (fixed #42)

`should_drop_batch_for_reconnect_handshake`:

- **2p:** drop while resync handshake flags are set (session-global).
- **Multi-peer:** drop only batches from `pending_reconnect_player_slot_`.

`is_valid_remote_player_slot` rejects out-of-range / self slots before ready-bit maps.

## Duplicate ResyncReady + ManualControlTag (fixed #44)

- Host ignores duplicate `ResyncReady` after handshake complete (no second full reset).
- Client retries `ResyncReady` only while still awaiting handshake.
- `set_player_ai_controlled(slot, true)` clears `ManualControlTag` only for that slot’s workers.

## Stale render snapshot (fixed #46)

Restore frame clears selection before input uses the new snapshot (see Graphical tick thread).

## Host TickStateHash fan-out (fixed #48; relay still missing)

For `session_player_count_ > 2`, host `send_state_hash` broadcasts to all clients.
Clients send hashes only to the host. Host does **not** relay client hashes to other clients.
Client-side verify waits on `required_remote_slots_mask()`; missing peer hashes mean
verification never completes on clients (no false desync, no client↔client wire check).

## Multi-peer AI fallback (fixed #59; 2p path kept)

| Path | Behavior |
|------|----------|
| 2p | `enter_ai_fallback()` → global `ai_fallback_`; solo advance |
| Multi-peer | per-slot AI via `handle_slot_ai_takeover` + `ai_controlled_slots_mask_` |
| AI batches | Host builds AI commands, broadcasts, applies via `pending_ai_input_batches_` |

Regression: `--lockstep-4-disconnect-smoke` (`27203`), `--lockstep-4-reconnect-smoke` (`27207`).

## Host local outbox (fixed #64 / silence path)

`handle_peer_connected`, `handle_resync_ready`, and `enter_ai_fallback` flush
`local_outbox_` into the input log **before** `reset_tick_sync_state()`.

Mid-match reconnect re-arms snapshot send when a retrying client needs a fresh blob.

## Silence-based AI (disconnect path fixed post-#67)

`is_opponent_unresponsive()`: host waiting on a remote batch longer than
`LOCKSTEP_PEER_SILENCE_MS` (3000).

On 2p AI takeover, host always `disconnect_peer()` when still connected so the silent
client can enter host-lost / reconnect (grace no longer skips that disconnect).

`--lockstep-peer-silence-smoke` (`27204`) covers warmup → silence → AI → disconnect.

## Reconnect flow

Initial join: client `ReconnectRequest` → host `JoinAccepted`.

Mid-match: host `ReconnectSnapshot` → client apply → `ResyncReady` → host resumes that slot.

### Snapshot blob

Magic `0x414F4153` (`AOAS`), `SNAPSHOT_VERSION = 26` (`sim_snapshot.cpp`).

| API | Role |
|-----|------|
| `encode_sim_snapshot` | Serialize checkpoint |
| `apply_sim_snapshot` | Restore into a `Simulation` |
| `decode_sim_snapshot_metadata` | Metadata without mutating a live sim |
| `validate_snapshot_input_replay` | Replay-only hash check |

`apply_sim_snapshot` resets via scenario load, then overlays map/fog/entities / map pings.
It does **not** rebuild the world by replaying `input_log`; the log (including unapplied /
pending orders) is restored into `CommandQueue` only. Entity ids are not stable across
restore — use `EntitySnapshotKey`.

Alpha_v0.2 reconnect fix: snapshot restore must not inflate `player_stats` via `spawn_*`
(that caused immediate post-reconnect desync). Alpha_v0.2.1 also restores map pings and
pending command state so a second drop does not freeze or DESYNC the handshake.

## Resign and host leave (alpha_v0.2.1)

| Action | API | Effect |
|--------|-----|--------|
| **Resign** (stay in match) | `request_match_resign` → `PlayerResign` | Slot marked resigned; no AI takeover; slot not reclaimable; chat announces resign |
| **Leave / Exit to menu** | `request_voluntary_resign` | Same resign marking; host also sends `HostEnded` then disconnects |
| **HostEnded** | clients set `host_gone_` | Match ends for everyone — no reconnect to a dead host |

Resigned / eliminated slots skip AI takeover in `handle_host_player_slot_disconnected`.

## Wire messages

Lockstep / reconnect kinds used in live matches (`src/net/net_message.hpp`):

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
| `SlotAiTakeover` | 10 | reliable |
| `SlotAiResume` | 11 | reliable |
| `Chat` | 12 | reliable |
| `MatchPause` | 21 | reliable |
| `PlayerResign` | 22 | reliable |
| `HostEnded` | 23 | reliable |

Lobby kinds `13`–`20` are used by `LobbySession` before match start. `CHANNEL_RELIABLE = 0`,
`CHANNEL_UNRELIABLE = 1`.

## Network HUD

`PING: Nms` from `LockstepSession::network_hud_stats()` → `hud_overlay.cpp`.

| Field | Behavior |
|-------|----------|
| `local_ping_ms` | Min of last `LOCKSTEP_PING_DISPLAY_WINDOW` (8) samples; else latest; else smoothed RTT |
| `peer_latency_ms` | Local slot filled with `local_ping_ms`; HUD prefers a positive per-slot value when present |

Probes every `LOCKSTEP_LATENCY_PROBE_INTERVAL_MS` (200).

## Combat sort key

Attack / chase / combat systems sort with `snapshot::sort_entities_by_snapshot_key`
(`EntitySnapshotKey`: slot + category + ordinal), not raw `entt::entity` id.

## Runtime data paths

Compile definition `AOA_RUNTIME_ROOT="${CMAKE_SOURCE_DIR}"` (`CMakeLists.txt`).
There is **no** `AOA_DATA_DIR` env var.

`default_data_directory()` searches: exe/`data` → `AOA_RUNTIME_ROOT`/`data` → cwd `data`.
Release POST_BUILD packs `assets.dat` (assets + data), removes loose `assets/`/`data/` next
to the exe, and copies `scenarios/` + `patterns/`. Portable LAN folders must ship
`assets.dat`, not loose trees — see [LAN_SOAK.md](LAN_SOAK.md).

## Remaining gaps / pitfalls

| Item | Status in code |
|------|----------------|
| `--lockstep-8-smoke` | Not present |
| Client↔client `TickStateHash` relay | Missing; host fan-out only (#48) |
| Host migration | Policy only ([DECISIONS.md](DECISIONS.md)) |
| Mid-barrier multi-peer AI takeover | `handle_host_player_slot_disconnected` / resync timeout still call `reset_tick_sync_state()` after per-slot AI; can wipe live ready bits for other peers (draft fix PR #72 — not merged) |
| CI smoke exit codes | PowerShell Headless smoke step does not fail the job on native non-zero exits |
| Resign vs disconnect | Resign / leave marks the slot and skips AI; unexpected drop still takes AI |

## Related

- [BUILD.md](BUILD.md) — presets and short CLI table
- [HARNESS.md](HARNESS.md) — scenario hashes and command replay
- [LAN_SOAK.md](LAN_SOAK.md) — two-PC soak checklist
- [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md) — 4–8 player scale plan
- [ECS.md](ECS.md) — tick pipeline after commands apply
- [DECISIONS.md](DECISIONS.md) — command timing, disconnect policy
- [RELEASE_NOTES.md](../RELEASE_NOTES.md) — alpha_v0.2.1 resign / reconnect notes
