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

# Snapshot encode/restore suite (no network)
.\build\x64-debug\Debug\aoa.exe --snapshot-smoke
.\build\x64-debug\Debug\aoa.exe --snapshot-double-spawn-smoke
.\build\x64-debug\Debug\aoa.exe --snapshot-reconnect-smoke
.\build\x64-debug\Debug\aoa.exe --snapshot-heavy-smoke

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
| `--snapshot-smoke` | Encode/restore after gather, AI, and map mutation; hash match for 10 more ticks |
| `--snapshot-double-spawn-smoke` | Host self-apply + client restore, then two wire-encoded `SpawnWorker`s stay in sync |
| `--snapshot-reconnect-smoke` | Long run (~999 ticks) with spawns + AI, then encode/restore once |
| `--snapshot-heavy-smoke` | Busy two-slot workload (~760 ticks), then encode/restore |
| `--lockstep-host` | Listen; graphical unless `--headless` |
| `--lockstep-join HOST:PORT` | Connect; graphical unless `--headless` |
| `--port PORT` | Host listen port (default `27000`) |
| `--ticks N` | Stop after N sim ticks (headless default `100`) |
| `--headless` | No window |
| `--lockstep-debug` | Write `logs/lockstep_*.log` next to the binary |

CI runs `--net-smoke`, `--lockstep-smoke`, `--lockstep-disconnect-smoke`, `--lockstep-reconnect-smoke`, `--lockstep-4-smoke`, and `--snapshot-smoke` on both x64 presets (see `.github/workflows/build.yml`). The other `--snapshot-*-smoke` flags are local-only stress checks in `src/net/lockstep_runner.cpp`; they are not in the workflow yet.

`main.cpp` dispatches exclusive modes in this order: harness → net/lockstep/snapshot smokes → lockstep host/join → headless → graphical. The first matching flag wins.

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

`enter_ai_fallback()` always sets `ai_controlled_slot_ = opponent_player_slot()`. That helper returns slot `1` for the host and slot `0` for a client. It does not take the disconnected peer's slot. Transport already exposes `consume_peer_lost_slot(lost_client_slot)`, but `poll()` still calls `consume_peer_lost()` and never reads which client dropped.

In a 2-player session that mapping is correct. In a 4-player session, losing slot 2 or 3 still AI-controls slot 1 and flips the global `ai_fallback_` flag. While `ai_fallback_` is set, `try_advance_tick()` takes the AI path (`try_advance_ai_fallback_tick`) instead of waiting on every remote batch. Only `ai_controlled_slot_` gets `inject_ai_commands`, and those AI batches stay host-local (no broadcast to remaining peers). Still-connected clients keep live N-way gating against a host that is no longer waiting on their slots — guaranteed desync or stall. Subsequent peer-loss events are ignored once `ai_fallback_` is set.

Do not treat multi-peer disconnect AI as shipped. Open fix PR #59 tracks per-slot AI, lost-slot consumption, AI batch broadcast, and a `--lockstep-4-disconnect-smoke` regression; none of that is on `main` yet.

`Simulation::set_player_ai_controlled(slot, true)` marks the slot in `MatchSession`, then removes `ManualControlTag` from **every** `WorkerUnitTag` in the registry. It does not filter by `PlayerSlot`. When the host is microing its own workers and the opponent disconnects, those host workers lose manual control and fall back to auto-gather until the player re-issues orders. Clearing AI (`enabled=false`) does not restore the tags. Open fix PR #44 scopes the tag clear to the AI slot.

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

Snapshot payload includes tick count, state hash, command sequence, AI-control metadata, full `input_log`, map/fog, and entity records (`src/sim/snapshot/sim_snapshot.cpp`, magic `0x414F4153`, version `9`). Before sending, the host validates a local roundtrip and applies the same bytes to itself so both peers share one checkpoint.

Client reconnect pacing: every `LOCKSTEP_RECONNECT_INTERVAL_MS` (500), up to `LOCKSTEP_RECONNECT_MAX_ATTEMPTS` (20).

### Duplicate ResyncReady after live resume

After the client restores a snapshot it sends `ResyncReady`, then `maybe_retry_resync_ready()` keeps re-sending for `LOCKSTEP_RESYNC_READY_RETRY_WINDOW_MS` (10s) every `LOCKSTEP_RESYNC_READY_RETRY_MS` (500). That retry path only checks `resync_ready_sent_`. It does not stop once the host has cleared `awaiting_reconnect_handshake_` and resumed live batches.

On the host, `handle_resync_ready()` always calls `reset_tick_sync_state()` for an accepted slot. A late duplicate after live resume can wipe ready-bit state mid-match and stall both peers. Separately, `poll()` returns early when `desynced_` is set, before `consume_peer_lost()` runs, so a desync flag can block disconnect/AI recovery until the session ends.

Two-player LAN reconnect often looks fine until the retry window overlaps live play. Watch `logs/lockstep_*.log` for `resync_ready_retry` after `reconnect_bootstrap_complete`. Fix work for this path is tracked outside this docs PR; do not treat post-reconnect freezes as "expected grace timeout" without checking those log lines.

### Stale render snapshot used for input

Graphical lockstep runs input and render off a published `SimRenderSnapshot` (`LockstepSession::render_snapshot()`), not a live registry read. After a mid-match restore, `apply_reconnect_snapshot` republishes that snapshot and sets `snapshot_restored_pending_`.

On `main` today, `run_graphical_lockstep` in `application.cpp` captures `input_snapshot` at the top of the frame, polls events with that pointer, then only later calls `consume_snapshot_restored()` (clear selection / reset camera) and `update_continuous`. If the background tick thread published the post-restore snapshot during that window, event handling still used the pre-restore pointer. Entity IDs can be recycled across restore, so clicks and continuous move orders can target the wrong units and desync lockstep.

Required order on the restore frame: service latency → consume snapshot-restored → capture `input_snapshot` → poll events / continuous input. Open fix PR #46 reorders that loop. Until it merges, treat post-reconnect mis-clicks on a graphical client as this bug, not as bad mouse hit-testing.

### Peer targeting today

`player_slot` is game identity. ENet client index is transport identity (`client_peers_[1..max_clients]`). They are not always equal when clients connect out of order.

Current host send rules in `lockstep_session.cpp`:

| Message | `session_player_count_ == 2` | `session_player_count_ > 2` |
|---------|------------------------------|-----------------------------|
| `JoinAccepted` | `send_reliable()` (only one client) | `send_reliable_to_client(player_slot, …)` |
| `ReconnectSnapshot` | `send_reliable()` | still `send_reliable()` |
| Host `TickInputBatch` | `send_reliable()` | `broadcast_reliable_except(..., nullopt)` |
| Relayed remote `TickInputBatch` | n/a | `broadcast_reliable_except(..., batch.player_slot)` |
| `TickStateHash` | `send_unreliable()` | still `send_unreliable()` |

`send_reliable()` / `send_unreliable()` without a target deliver to the first connected client peer (`client_peers_` scan from slot 1). That is fine for 2-player. With 3+ peers still connected:

- `handle_reconnect_request` calls `send_join_accepted(player_slot)` with the **game** slot from the request. For `session_player_count_ > 2` that value is passed straight to `send_reliable_to_client` as an ENet client index. Out-of-order joins can ACK the wrong peer.
- Mid-match `ReconnectSnapshot` can land on the wrong client (untargeted `send_reliable()`).
- Host `TickStateHash` only reaches the first connected client, so other peers may never receive the host hash (desync detection gap, not an immediate crash). Open fix PR #48 adds host `broadcast_unreliable_except` for that fan-out; it is not on `main` yet.
- Relayed batches pass `batch.player_slot` as `except_client_slot`. Transport slots are `1..max_clients`; game slots are `0..N-1`. Without a player→ENet map, the except filter can skip the wrong peer or none at all.

`--lockstep-4-smoke` advances four sessions and compares each local `Simulation::state_hash()` after the run. It does not exercise reconnect, multi-peer disconnect, multi-peer hash fan-out, or wire desync detection, so CI can stay green while those paths are broken. Track player→ENet mapping in open PR #40 and multi-peer disconnect AI in open PR #59. Do not treat 4-peer reconnect, 4-peer disconnect AI, or 4-peer wire desync detection as proven yet.

### Resync batch gate today

While the host waits for mid-match `ResyncReady`, `process_received_packet` drops every inbound `TickInputBatch` when `!client_resync_ready_` or `awaiting_reconnect_handshake_`. Those flags are session-global, not scoped to the reconnecting slot. Connected peers that are still live lose their batches too, so the match freezes for everyone until resync finishes or fails.

That is invisible in 2-player reconnect smoke (only one remote). It bites as soon as `session_player_count_ > 2`. Fix work is tracking a per-slot pending reconnect gate and validating wire `player_slot` before indexing ready-bit masks. Until that lands, do not run mid-match reconnect against a 4-peer host.

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

`verify_state_hash` waits until every remote slot in `required_remote_slots_mask()` has a hash for that tick, then compares. Clients send hashes only to the host. The host does **not** relay peer hashes to other clients. In a 4-player session a client therefore never fills hashes for the other client slots, so client-side wire verification returns early and never completes. After open PR #48 merges, the host can fan hashes out to every client, but clients still cannot compare client↔client hashes on the wire unless a relay lands later. Host-side comparison of all client hashes is the path that can work once fan-out is fixed.

On mismatch:

- `is_desynced()` becomes true
- stderr / debug log print `lockstep desync at tick N: local=0x… remote=0x…`
- further tick advance no-ops
- headless runners return non-zero
- graphical sessions keep the desync state until the window closes (exit code 1)

Hash contents match the harness (`compute_state_hash`). See [HARNESS.md](HARNESS.md).

In sessions with `session_player_count_ > 2`, host hash fan-out is incomplete today (`send_unreliable()` → first client only; see Peer targeting today). A silent miss means a diverging peer may keep advancing until some other path notices. Do not rely on wire hash exchange alone to prove multi-peer determinism; use `--lockstep-4-smoke` (local hash compare after advance) plus explicit local checks.

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
| Match freezes a few seconds after a successful reconnect | Late duplicate `ResyncReady` retries call `reset_tick_sync_state()` after live resume; see Duplicate ResyncReady after live resume |
| Host workers stop obeying micro after opponent disconnect | `set_player_ai_controlled` strips `ManualControlTag` from all workers, not only the AI slot |
| Expecting 4 graphical clients via `--lockstep-host` | Not yet. Host still uses 2-player session; use `--lockstep-4-smoke` for N-way gating |
| `--player-slot` rejected | Flag not implemented; only slots 0/1 via host/join |
| Hash mismatch with no local commands | Non-deterministic sim change, spawn order, or float in sim state |
| Slot 2/3 reconnect hangs or corrupts another peer | Host still broadcasts `ReconnectSnapshot` with untargeted `send_reliable()`; see Peer targeting today |
| 4-peer host hash never arrives on clients 2/3 | Host `TickStateHash` uses untargeted `send_unreliable()`; see Peer targeting today |
| Wrong peer excluded from batch relay | Host relay passes game `player_slot` as ENet `except_client_slot`; see Peer targeting today |
| All peers freeze during one client's reconnect | Host drops every `TickInputBatch` while `client_resync_ready_` is false; see Resync batch gate today |
| Wrong army goes AI after a non-P2 disconnect | `enter_ai_fallback()` uses `opponent_player_slot()` (always slot 1 for host), not the lost peer; open fix #59 |
| 4-peer match desyncs/stalls after any client drop | Global `ai_fallback_` leaves remaining peers in live gating while host solo-advances; AI batches stay host-local; open fix #59 |
| JoinAccepted never reaches the joining client (3+ peers) | Host passes game `player_slot` as ENet client slot; see Peer targeting today |
| Client never reports wire desync in 4p | No TickStateHash relay; `verify_state_hash` waits on every remote slot; see Desync detection |
| After reconnect, clicks move the wrong units | Frame used pre-restore `SimRenderSnapshot` for input; see Stale render snapshot used for input |
| Snapshot smoke fails after sim change | Entity keys / fog / AI metadata must roundtrip; run the full local snapshot suite before LAN |
| Desync banner, then disconnect never starts AI | `poll()` returns on `desynced_` before peer-loss handling; recovery waits on the open fix |

## Related

- [BUILD.md](BUILD.md). Presets and short CLI table.
- [HARNESS.md](HARNESS.md). Scenario hashes and command replay.
- [LAN_SOAK.md](LAN_SOAK.md). Two-PC soak checklist.
- [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md). 4–8 player scale plan.
- [ECS.md](ECS.md). Tick pipeline after commands apply.
- [DECISIONS.md](DECISIONS.md). Command timing, disconnect policy.
