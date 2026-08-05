# Lockstep networking (M2)

Runbook for the 2-player ENet lockstep path. Same `aoa` binary as singleplayer and the harness.

## Intent

Two peers run the same deterministic sim. Each peer sends its inputs for a future execute tick; neither advances that tick until both input batches are present. After every tick, peers exchange `state_hash` and stop on mismatch.

This is the daily multiplayer loop for M2. Reconnect / snapshot catch-up is still open (see [BACKLOG.md](BACKLOG.md)).

## Roles and slots

| Role | CLI | Player slot |
|------|-----|-------------|
| Host | `--lockstep-host` | `0` (`LOCKSTEP_HOST_PLAYER_SLOT`) |
| Client | `--lockstep-join HOST:PORT` | `1` (`LOCKSTEP_CLIENT_PLAYER_SLOT`) |

Constants live in `src/net/net_constants.hpp`.

| Port constant | Value | Used by |
|---------------|-------|---------|
| `DEFAULT_PORT` | `27000` | Graphical/headless host default; `--net-smoke` |
| `LOCKSTEP_SMOKE_PORT` | `27001` | `--lockstep-smoke` only |

`player_slot` is wire identity and command-sort key. Both peers still load the same default Earth scenario with the same `PlayerOwnedTag` units. Slot does not mean opposing-faction ownership yet.

## CLI

Build first ([BUILD.md](BUILD.md)), then:

```powershell
# Transport only — loopback host/client on 27000, one reliable PlayerCommand
.\build\x64-debug\Debug\aoa.exe --net-smoke

# Two LockstepSessions in-process on 27001; host gather at tick_count 3 (execute 5); hash match
.\build\x64-debug\Debug\aoa.exe --lockstep-smoke

# Graphical 2p (default for host/join)
.\build\x64-debug\Debug\aoa.exe --lockstep-host --port 27000
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000

# Headless scripted run (default 100 ticks)
.\build\x64-debug\Debug\aoa.exe --lockstep-host --headless --ticks 100
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000 --headless --ticks 100
```

| Flag | Effect |
|------|--------|
| `--net-smoke` | In-process `EnetTransport` connect + reliable send on `27000` |
| `--lockstep-smoke` | In-process host+client sessions on `27001`, 30 ticks, fail on desync |
| `--lockstep-host` | Listen; graphical unless `--headless` |
| `--lockstep-join HOST:PORT` | Connect; graphical unless `--headless` |
| `--port PORT` | Host listen port (default `27000`) |
| `--ticks N` | Stop after N sim ticks (headless default `100`; graphical optional) |
| `--headless` | No window; used with host/join for scripted runs |

CI runs `--net-smoke` and `--lockstep-smoke` on both x64 presets (see `.github/workflows/build.yml`).

Graphical host/join wait until the peer connects (no attempt cap). Headless host/join and `--lockstep-smoke` give up after `LOCKSTEP_CONNECT_ATTEMPTS` polls.

## Architecture

```
GameInput (pick → PlayerCommand)
  → LockstepSession::submit_local_command   // buffer in local_outbox_
  → ensure_local_batch_sent                 // TickInputBatch on reliable channel
  → wait for remote TickInputBatch
  → flush_local_commands_for_tick           // enqueue_network_command
  → Simulation::tick()                      // apply_pending + gameplay + hash
  → TickStateHashMessage both ways
```

Codepaths:

| Piece | Location |
|-------|----------|
| Transport | `src/net/enet_transport.*` |
| Wire envelope | `src/net/net_message.*` (`NetMessageKind`) |
| Batch / hash codec | `src/net/lockstep_wire.*` |
| Session | `src/net/lockstep_session.*` |
| Headless host/join + smoke | `src/net/lockstep_runner.*` |
| Graphical loop | `src/app/application.cpp` (`run_graphical_lockstep`) |
| Input routing | `src/app/game_input.cpp` (`set_lockstep_session`) |

Singleplayer still calls `Simulation::enqueue_player_command` directly. Lockstep never enqueues local commands until the outbound batch for that execute tick has been sent and the remote batch is ready.

## Input delay

| Mode | Constant | Value | Meaning |
|------|----------|-------|---------|
| Singleplayer / harness | `PLAYER_COMMAND_DELAY_TICKS` | `1` | Issue during tick *N* → execute at *N+1* |
| Lockstep | `LOCKSTEP_COMMAND_DELAY_TICKS` | `2` | Earliest execute = `tick_count + 2` |

`submit_local_command` also bumps `execute_tick` forward if that tick's local batch was already sent. That keeps late clicks in an outbound batch both peers still share.

Do not enqueue locally before the batch goes out. PR #33 fixed a desync where move commands hit the local sim early while the peer still applied them from the wire at the shared execute tick.

## Tick advance

`LockstepSession::try_advance_tick()`:

1. `execute_tick = simulation.tick_count() + 1`
2. Send local `TickInputBatch` for that tick if not yet sent (empty batch is valid)
3. Return false until a remote batch for the same tick arrived
4. Flush local outbox into `enqueue_network_command`
5. `simulation.tick()`
6. Send `TickStateHash` for the completed tick and compare with any remote hash already stored

Remote batches enqueue with the batch's `player_slot` and `execute_tick` forced from the wire header.

If the peer drops, `is_connected()` becomes false and advance stops. This tree does not implement AI takeover or reconnect yet (policy only in [DECISIONS.md](DECISIONS.md)).

## Wire messages

Envelope: `NetMessageKind` byte + payload (`encode_net_message` / `decode_net_message`).

| Kind | Value | Payload |
|------|-------|---------|
| `PlayerCommand` | 1 | Single command (used by `--net-smoke`) |
| `TickStateHash` | 2 | execute tick, player slot, `uint64` hash |
| `TickInputBatch` | 3 | execute tick, player slot, command count, length-prefixed `PlayerCommand` blobs |

Reliable channel only (`CHANNEL_RELIABLE = 0`). Max peers today: `2`.

## Desync detection

After each completed tick, both peers store and exchange hashes. On mismatch:

- `is_desynced()` becomes true
- stderr prints `lockstep desync at tick N: local=0x… remote=0x…`
- further poll / submit / advance no-ops
- graphical host/join sets the window title to `DESYNC tick N`, then exits with code 1

Hash contents match the harness (`compute_state_hash`). See [HARNESS.md](HARNESS.md).

## Common pitfalls

| Symptom | Likely cause |
|---------|----------------|
| Desync right after issuing a move/attack | Local apply bypassed the batch (should go through `submit_local_command` only) |
| Peer never advances | Missing remote `TickInputBatch` for `tick_count + 1`; check connect / firewall / port |
| `lockstep-join: invalid address` | Address must be `HOST:PORT` |
| `--lockstep-smoke` fails while manual host works | Smoke binds `27001`; leave that port free (`--net-smoke` uses `27000`) |
| Headless host times out | Client must join before `LOCKSTEP_CONNECT_ATTEMPTS` polls finish |
| Graphical host sits on "Waiting for opponent..." | Expected until a client joins; there is no attempt timeout |
| Commands feel one tick later than singleplayer | Expected. Lockstep delay is 2 ticks, not 1 |
| Both windows control the same units | Expected for now. Shared Earth/`PlayerOwnedTag` spawn; slot is not a second faction |
| Hash mismatch with no local commands | Non-deterministic sim change, spawn order, or float in sim state |

## Related

- [BUILD.md](BUILD.md) — presets and short CLI table
- [HARNESS.md](HARNESS.md) — scenario hashes and command replay
- [ECS.md](ECS.md) — tick pipeline after commands apply
- [DECISIONS.md](DECISIONS.md) — command timing, disconnect policy
