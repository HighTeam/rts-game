# Lockstep networking

Runbook for ENet lockstep: 2-player graphical play, reconnect/AI fallback, and the 4-peer CI smoke. Same `aoa` binary as singleplayer and the harness.

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
|---------------|-------|---------|
| `DEFAULT_PORT` | `27000` | Graphical/headless host default; `--net-smoke` |
| `LOCKSTEP_SMOKE_PORT` | `27001` | `--lockstep-smoke` |
| `LOCKSTEP_DISCONNECT_SMOKE_PORT` | `27200` | `--lockstep-disconnect-smoke` |
| `LOCKSTEP_RECONNECT_SMOKE_PORT` | `27201` | `--lockstep-reconnect-smoke` |
| `LOCKSTEP_4_SMOKE_PORT` | `27202` | `--lockstep-4-smoke` |

`player_slot` is wire identity, command ownership, and snapshot-key sort order. The default Earth scenario spawns a Town Center, worker, and militia for slot 0 and slot 1. Selection and commands filter by `PlayerSlot`. There is no `--player-slot` CLI yet.

Graphical and headless `--lockstep-host` / `--lockstep-join` build a 2-player session (`LOCKSTEP_PLAYER_COUNT`). The host accepts one client. `--lockstep-4-smoke` is the only shipped path that opens a 4-player session today.

## CLI

Build first ([BUILD.md](BUILD.md)), then:

```powershell
# Transport only: loopback on 27000
.\build\x64-debug\Debug\aoa.exe --net-smoke

# Two LockstepSessions in-process on 27001; scripted commands; hash match
.\build\x64-debug\Debug\aoa.exe --lockstep-smoke

# Disconnect → AI fallback on host
.\build\x64-debug\Debug\aoa.exe --lockstep-disconnect-smoke

# Three disconnect → AI → reconnect → live cycles
.\build\x64-debug\Debug\aoa.exe --lockstep-reconnect-smoke

# Four peers in-process; empty batches; hash match after 40 ticks
.\build\x64-debug\Debug\aoa.exe --lockstep-4-smoke

# Snapshot encode/restore (no network)
.\build\x64-debug\Debug\aoa.exe --snapshot-smoke

# Graphical 2p
.\build\x64-debug\Debug\aoa.exe --lockstep-host --port 27000
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000

# Headless scripted run (default 100 ticks)
.\build\x64-debug\Debug\aoa.exe --lockstep-host --headless --ticks 100
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000 --headless --ticks 100
```

| Flag | Effect |
|------|--------|
| `--net-smoke` | In-process `EnetTransport` + reliable `PlayerCommand` on `27000` |
| `--lockstep-smoke` | 2 sessions on `27001`, scripted gather/spawn, fail on desync |
| `--lockstep-disconnect-smoke` | Client drops; host enters AI immediately and keeps ticking |
| `--lockstep-reconnect-smoke` | 3× disconnect → AI → snapshot reconnect → live lockstep |
| `--lockstep-4-smoke` | Host + 3 clients on `27202`, 40 ticks, hash match |
| `--snapshot-smoke` | Encode/restore sim snapshot + input log; no ENet |
| `--lockstep-host` | Listen; graphical unless `--headless` |
| `--lockstep-join HOST:PORT` | Connect; graphical unless `--headless` |
| `--port PORT` | Host listen port (default `27000`) |
| `--ticks N` | Stop after N sim ticks (headless default `100`) |
| `--headless` | No window |
| `--lockstep-debug` | Write `logs/lockstep_*.log` next to the binary |

CI runs `--net-smoke`, `--lockstep-smoke`, `--lockstep-disconnect-smoke`, `--lockstep-reconnect-smoke`, `--lockstep-4-smoke`, and `--snapshot-smoke` on both x64 presets (see `.github/workflows/build.yml`).

Graphical host/join wait until the peer connects. Headless host/join and smokes give up after `LOCKSTEP_CONNECT_ATTEMPTS` polls.

Two-machine checklist: [LAN_SOAK.md](LAN_SOAK.md).

## Architecture

```
GameInput (pick → PlayerCommand)
  → LockstepSession::submit_local_command   // buffer in local_outbox_
  → ensure_local_batch_sent                 // TickInputBatch on reliable channel
  → wait until every remote slot has a batch
  → flush_local_commands_for_tick           // enqueue_network_command
  → Simulation::tick()                      // apply_pending + gameplay + hash
  → TickStateHash (unreliable) both ways
```

| Piece | Location |
|-------|----------|
| Transport | `src/net/enet_transport.*` |
| Wire envelope | `src/net/net_message.*` (`NetMessageKind`) |
| Batch / hash / latency codec | `src/net/lockstep_wire.*` |
| Reconnect request codec | `src/net/reconnect_wire.*` |
| Snapshot encode/restore | `src/sim/snapshot/sim_snapshot.*` |
| Session | `src/net/lockstep_session.*` |
| Headless host/join + smokes | `src/net/lockstep_runner.*` |
| Graphical loop | `src/app/application.cpp` (`run_graphical_lockstep`) |
| Input routing | `src/app/game_input.cpp` (`set_lockstep_session`) |
| Disconnect AI commands | `src/sim/systems/disconnected_player_ai.*` |

Singleplayer still calls `Simulation::enqueue_player_command` directly. Lockstep never enqueues local commands until the outbound batch for that execute tick has been sent and remote batches are ready.

## Input delay

| Mode | Constant | Value | Meaning |
|------|----------|-------|---------|
| Singleplayer / harness | `PLAYER_COMMAND_DELAY_TICKS` | `1` | Issue during tick *N* → execute at *N+1* |
| Lockstep | `LOCKSTEP_COMMAND_DELAY_TICKS` | `2` | Earliest execute = `tick_count + 2` |

These are separate constants. Lockstep does not stack both.

`submit_local_command` also bumps `execute_tick` forward if that tick's local batch was already sent. That keeps late clicks in an outbound batch both peers still share.

Do not enqueue locally before the batch goes out. PR #33 fixed a desync where move commands hit the local sim early while the peer still applied them from the wire at the shared execute tick.

## Tick advance

`LockstepSession::try_advance_live_tick()`:

1. `execute_tick = simulation.tick_count() + 1`
2. Send local `TickInputBatch` for that tick if not yet sent (empty batch is valid and required)
3. Return false until every remote slot in `0 .. session_player_count-1` (except self) has a batch
4. Flush local outbox into `enqueue_network_command`
5. `simulation.tick()`
6. Send `TickStateHash` for the completed tick
7. Compare remote hashes when verification is active

Remote batches enqueue with the batch's `player_slot` and `execute_tick` forced from the wire header. The session trusts the batch header slot, not the ENet sender index.

### AI fallback

If the client drops after the match has started, the host:

1. Starts `LOCKSTEP_RECONNECT_GRACE_MS` (30s)
2. Calls `enter_ai_fallback()` on the same tick transport drops
3. Advances with `try_advance_ai_fallback_tick()`, which injects `generate_ai_commands_for_slot()` before `simulation.tick()`
4. Does not send input batches or state hashes while in AI fallback

Disconnect AI currently targets the 2-player opponent slot helper (`opponent_player_slot()`). Multi-peer AI for slots 2+ is not finished.

If the host drops, the client stops simulating, retries reconnect, and does **not** AI-control the host. Host migration remains M3.

## Reconnect

Initial join (before match ticks):

1. Client connects over ENet and sends `ReconnectRequest`
2. Host replies `JoinAccepted`
3. Client marks `session_ready_`

Mid-match reconnect:

1. Client reconnects; host sees `match_started_` and sends `ReconnectSnapshot` (bytes from `sim::encode_sim_snapshot()`)
2. Client may also send `ReconnectRequest`; host can retry the snapshot (debounced by `LOCKSTEP_RECONNECT_SNAPSHOT_DEBOUNCE_MS`)
3. Client applies the snapshot, then sends `ResyncReady`
4. Host clears AI for that slot, resets sync state, and resumes live batches

`JoinAccepted` is not part of the normal mid-match snapshot path.

Snapshot payload includes tick count, state hash, command sequence, AI-control metadata, full `input_log`, map/fog, and entity records (`src/sim/snapshot/sim_snapshot.cpp`, magic `0x414F4153`, version `9`).

Client reconnect pacing: every `LOCKSTEP_RECONNECT_INTERVAL_MS` (500), up to `LOCKSTEP_RECONNECT_MAX_ATTEMPTS` (20).

## Wire messages

Envelope: `NetMessageKind` byte + payload (`encode_net_message` / `decode_net_message`).

| Kind | Value | Channel | Payload |
|------|------:|---------|---------|
| `PlayerCommand` | 1 | reliable | Single command (`--net-smoke`) |
| `TickStateHash` | 2 | unreliable | execute tick, player slot, `uint64` hash |
| `TickInputBatch` | 3 | reliable | execute tick, player slot, command count, length-prefixed commands |
| `ReconnectRequest` | 4 | reliable | player slot |
| `ReconnectSnapshot` | 5 | reliable | snapshot blob |
| `JoinAccepted` | 6 | reliable | empty / join ack |
| `ResyncReady` | 7 | reliable | player slot ready after restore |
| `LatencyProbe` | 8 | unreliable | send time + sequence |
| `LatencyPong` | 9 | unreliable | echoed probe |

`CHANNEL_RELIABLE = 0`, `CHANNEL_UNRELIABLE = 1`.

## Desync detection

Every live completed tick sends `TickStateHash`. Verification stays off during reconnect/AI/handshake and for `LOCKSTEP_HASH_VERIFY_WARMUP_TICKS` (30) after join, snapshot restore, or resync.

On mismatch:

- `is_desynced()` becomes true
- stderr / debug log print `lockstep desync at tick N: local=0x… remote=0x…`
- further tick advance no-ops
- headless runners return non-zero
- graphical sessions keep the desync state until the window closes (exit code 1)

Hash contents match the harness (`compute_state_hash`). See [HARNESS.md](HARNESS.md).

## Common pitfalls

| Symptom | Likely cause |
|---------|----------------|
| Desync right after issuing a move/attack | Local apply bypassed the batch (must go through `submit_local_command`) |
| Peer never advances | Missing remote `TickInputBatch` for `tick_count + 1`; check connect / firewall / port |
| `lockstep-join: invalid address` | Address must be `HOST:PORT` |
| Smoke fails while manual host works | Smokes bind `27001` / `27200+`; leave those ports free |
| Headless host times out | Client must join before `LOCKSTEP_CONNECT_ATTEMPTS` polls finish |
| Graphical host sits on "Waiting for opponent..." | Expected until a client joins |
| Commands feel one tick later than singleplayer | Expected. Lockstep delay is 2 ticks, not 1 |
| Client disconnect freezes the match | Should not happen on host; host must enter AI fallback immediately |
| Reconnect fails after ~30s | Host grace expired (`LOCKSTEP_RECONNECT_GRACE_MS`) |
| Expecting 4 graphical clients via `--lockstep-host` | Not yet. Host still uses 2-player session; use `--lockstep-4-smoke` for N-way gating |
| `--player-slot` rejected | Flag not implemented; only slots 0/1 via host/join |
| Hash mismatch with no local commands | Non-deterministic sim change, spawn order, or float in sim state |

## Related

- [BUILD.md](BUILD.md). Presets and short CLI table.
- [HARNESS.md](HARNESS.md). Scenario hashes and command replay.
- [LAN_SOAK.md](LAN_SOAK.md). Two-PC soak checklist.
- [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md). 4–8 player scale plan.
- [ECS.md](ECS.md). Tick pipeline after commands apply.
- [DECISIONS.md](DECISIONS.md). Command timing, disconnect policy.
