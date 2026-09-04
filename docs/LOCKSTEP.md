# Lockstep networking

Runbook for ENet lockstep: 2–8 player sessions, reconnect / AI fallback, and CI smokes.
Same `AgeofAffinities` binary as singleplayer and the harness.

Verified against `main` @ `alpha_v0.2.1` (`bfc6a29`): M2 reconnect stack (#40–#48, #59, #64, #67),
M3 multi-peer CLI (`--players` / `--player-slot`), `--lockstep-4-reconnect-smoke`, plus resign /
`HostEnded`, snapshot v26 (map pings), and second-reconnect handshake polish.
Open caveats (not fixed on `main`): draft PRs **#72**, **#75**, **#77**, **#79**, **#81**, **#83**,
**#85**, **#87**, **#89**, **#93**, and **#95** — see Remaining gaps.

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

Sim advance and snapshot publish run on a background tick loop. Picks should use
`render_snapshot()`, not a live registry read.

Frame order after #46: `service_network_latency` → `consume_snapshot_restored`
(clear selection / reset camera) → capture input snapshot → poll events.
That avoids using a pre-restore render snapshot for clicks after reconnect.

**Registry data race on `main` (draft fix PR #87 — not merged):** autosave / SFX drain /
match-announcement paths already take `simulation_access_mutex()`, but
`run_lockstep_match` still calls `handle_event`, `update_continuous`, and
`make_hud_context` on the UI thread **without** that lock while
`background_tick_loop` mutates `simulation.registry()`. Concurrent EnTT access is UB —
intermittent crashes or torn reads during normal graphical multiplayer.

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

## Resync batch gate (#42 partial; multi-peer drop still broken — #77)

`should_drop_batch_for_reconnect_handshake` (`lockstep_session.cpp`):

- **2p (`session_player_count_ <= 2`):** drop while `!client_resync_ready_` or
  `awaiting_reconnect_handshake_` (session-global). Mid-match sets
  `pending_reconnect_player_slot_` on the 2p path only.
- **Multi-peer today:** still keys off `pending_reconnect_player_slot_`. Mid-match
  multi-peer reconnect returns early via `send_reconnect_snapshot_to_client` and only
  sets `resync_pending_slots_mask_`, so the drop helper usually returns **false** and
  host can apply AI + wire batches for a reconnecting slot before `ResyncReady`
  (draft fix PR **#77** — drop when `(resync_pending_slots_mask_ & (1 << slot)) != 0`).

`is_valid_remote_player_slot` rejects out-of-range / self slots before ready-bit maps.
#42 fixed the “gate stalls every peer” / slot-validation issues; it did not wire
multi-peer drops to `resync_pending_slots_mask_`.

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

**`TickStateHash` slot impersonation on `main` (draft fix PR #89 — not merged):**
Unlike `Chat` (which overwrites `player_slot` from `sender_slot` on the host), incoming
`TickStateHash` trusts the payload `player_slot`. A client can store/relay a hash under
another player's slot and force a false session-wide desync. Draft **#89** binds the hash
to `sender_slot` and re-encodes the relayed wire with the corrected slot.

## Multi-peer AI fallback (fixed #59; 2p path kept)

| Path | Behavior |
|------|----------|
| 2p | `enter_ai_fallback()` → global `ai_fallback_`; solo advance |
| Multi-peer (intended) | `handle_slot_ai_takeover` sets `disconnected_slots_mask_` + sim AI flag; host injects via `inject_ai_commands_for_disconnected_slots` |
| AI batches | Host builds AI commands, broadcasts, applies via `pending_ai_input_batches_` |

**Host double-AI desync on `main` (draft fix PR #81 — not merged):**
`note_player_slot_transport_down` calls `handle_host_player_slot_disconnected` (inject path)
**and** `enable_slot_ai_control` (sets `ai_controlled_slots_mask_`). Each tick the host then runs
both `inject_ai_commands_for_disconnected_slots` and `prepare_ai_controlled_slots_for_tick`,
so AI mutates the registry twice while clients only inject once → state-hash divergence.

**Stale per-slot AI sequences on `main` (draft fix PR #93 — not merged):**
After `apply_reconnect_snapshot_locally` / host adopt, `sync_command_sequences_from_input_log`
advances the global counters from the restored input log but does **not** rebuild
`ai_command_sequence_by_slot_[]`. Inject then reuses low per-slot sequences that already
exist in the pending log → lockstep apply order diverges across peers.

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

After a peer resyncs, the host also broadcasts a catch-up snapshot to live clients so they
align tick/hash without starting a new handshake (`handle_reconnect_snapshot` catch-up
branch: clear `awaiting_reconnect_handshake_`, `ensure_local_batch_sent`).

**Bystander catch-up forces handshake on `main` (draft fix PR #95 — not merged):**
`handle_reconnect_snapshot` enters the handshake path when
`actively_reconnecting || awaiting_reconnect_handshake_ || state_changed`. Host catch-up
broadcasts always advance tick/hash for live bystanders, so `state_changed` is true and
non-reconnecting peers hit `reset_tick_sync_state()` / `discard_pending_received()` via
the handshake arm and spam `ResyncReady`. Draft **#95** gates handshake on
`actively_reconnecting || awaiting_reconnect_handshake_` only (still covers host snapshot
retries before `ResyncReady`).

Regression target: `--lockstep-4-reconnect-smoke` (`27207`).

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

**Match-outcome gap on `main` (draft fix PR #85 — not merged):** snapshot v26 and
`compute_state_hash` omit `eliminated_slots_mask`, `match_finished`, `winner_slot`,
`last_eliminating_slot`, and `finished_tick`. After a resign, a reconnecting peer can
restore those slots as still active while everyone else keeps them eliminated. Hash
checks miss it because those fields are not mixed.

## Resign and host leave (alpha_v0.2.1)

| Action | API | Effect |
|--------|-----|--------|
| **Resign** (stay in match) | `request_match_resign` → `PlayerResign` | Slot marked resigned; no AI takeover; slot not reclaimable; chat announces resign |
| **Leave / Exit to menu** | `request_voluntary_resign` | Same resign marking; host also sends `HostEnded` then disconnects |
| **HostEnded** | clients set `host_gone_` | Match ends for everyone — no reconnect to a dead host |

Resigned / eliminated slots skip AI takeover in `handle_host_player_slot_disconnected`.
`required_remote_slots_mask()` also skips resigned slots, so peers stop waiting for that
player's batches as soon as the resign lands. Until **#85** lands, reconnect after resign
can still desync match-outcome state (see Snapshot blob).

**Auth / desync caveats on `main` (draft fix PRs #79 / #81 / #87 / #89 — not merged):**

- Host `process_received_packet` forwards `PlayerResign` / `ResyncReady` using only the
  payload `player_slot` — it never compares that to `sender_slot`. Lobby traffic already
  rejects `message->player_slot != sender_slot`; lockstep does not. A connected client can
  force-resign another slot or complete someone else's resync handshake early.
- Host `TickInputBatch` accepts any valid remote `batch.player_slot` and then
  `bind_player_to_enet_slot(batch.player_slot, sender_slot)`. Chat already overwrites
  `message.player_slot = sender_slot`; control-message auth is proposed in **#79**, but
  batches were missed. A malicious peer can forge another human's slot (move/attack/build)
  and rebind that slot to their connection (draft fix PR **#81** — not merged).
- Host `TickStateHash` stores and (for 4+) relays hashes under the payload `player_slot`
  without binding to `sender_slot`. A client can impersonate another player's hash and
  force a false desync (draft fix PR **#89** — not merged).
- `SlotAiTakeover` / `SlotAiResume` apply `handle_slot_ai_*` from **any** sender with no
  host-only or `sender_slot` check (unlike lobby color/team). A client can forge takeover
  for a still-connected opponent; the host applies it locally without broadcasting, so
  peers keep waiting for that slot's batches → stall or desync (draft fix PR **#87** —
  not merged; host should ignore inbound slot-AI messages, clients accept only from host).
- After local `request_match_resign()`, the slot is marked resigned immediately, but
  `submit_local_command` still accepts input and pipelined non-`Resign` outbox commands
  can still send. Peers no longer wait for that slot → stall or desync.

## Wire messages

Lockstep / reconnect kinds used in live matches (`src/net/net_message.hpp`):

| Kind | Value | Channel |
|------|------:|---------|
| `PlayerCommand` | 1 | reliable (`--net-smoke`) |
| `TickStateHash` | 2 | unreliable — **no sender↔slot bind on host** (see Remaining gaps / #89) |
| `TickInputBatch` | 3 | reliable — **no sender↔slot bind on host** (see Remaining gaps / #81) |
| `ReconnectRequest` | 4 | reliable |
| `ReconnectSnapshot` | 5 | reliable |
| `JoinAccepted` | 6 | reliable |
| `ResyncReady` | 7 | reliable |
| `LatencyProbe` | 8 | unreliable |
| `LatencyPong` | 9 | unreliable |
| `SlotAiTakeover` | 10 | reliable — **no sender auth on `main`** (see #87) |
| `SlotAiResume` | 11 | reliable — **no sender auth on `main`** (see #87) |
| `Chat` | 12 | reliable |
| `MatchPause` | 21 | reliable — see Match pause (pipelining pitfall / #83) |
| `PlayerResign` | 22 | reliable |
| `HostEnded` | 23 | reliable |

Lobby kinds `13`–`20` are used by `LobbySession` before match start. `CHANNEL_RELIABLE = 0`,
`CHANNEL_UNRELIABLE = 1`.

## Match pause

Esc / game menu pause calls `LockstepSession::request_match_pause`, which sends `MatchPause`
on the reliable channel. While `match_paused_` is set, the background loop does not advance
live ticks.

**Pitfall on `main` (draft fix PR #83 — not merged):** `can_pipeline_batches` does not check
`match_paused_`. A paused peer can keep sending future `TickInputBatch` messages while peers
that are still live advance those ticks. Pause during LAN play can desync within one RTT.

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
| Coordinated MP save/load | Host-only local `autosave.mp.aoa`; UI Load disabled in lockstep — see [BUILD.md](BUILD.md) |
| Client↔client `TickStateHash` relay | Missing; host fan-out only (#48) |
| `TickStateHash` slot impersonation | Host stores/relays hashes under payload `player_slot` without binding to the ENet sender (Chat already binds). Draft fix PR **#89** — not merged |
| Host migration | Policy only ([DECISIONS.md](DECISIONS.md)) |
| Multi-peer resync batch drop | Mid-match multi-peer never sets `pending_reconnect_player_slot_`; batches from pending slots are not dropped (draft fix PR **#77** — not merged) |
| Resync handshake timeout stall | `send_reconnect_snapshot_to_client` ORs the slot into `resync_pending_slots_mask_` **then** resets `resync_handshake_started_` when that bit is set — so every 2s snapshot retry restarts the 15s timeout (draft fix PR **#77** — not merged) |
| Training refund on blocked spawn | `run_building_process_system` refunds only when `!player_can_spawn_units` (pop). If `try_complete_trained_unit_spawn` fails on a blocked tile, the building stays locked and cost is kept (draft fix PR **#77** — not merged; release notes already describe the intended refund) |
| Mid-barrier multi-peer AI takeover | `handle_host_player_slot_disconnected` / resync timeout still call `reset_tick_sync_state()` after per-slot AI; can wipe live ready bits for other peers (draft fix PR **#72** — not merged) |
| Reconnect `next_command_sequence` reset | `encode_sim_snapshot(..., include_pending_commands)` and `adopt_host_command_queue_from_reconnect_snapshot` recompute the sequence from the **unapplied** list only. An empty pending queue resets to `1`, so the host can reissue duplicate sequences while peers keep the full log (draft fix PR **#75** — not merged) |
| Late multi-peer `ResyncReady` | After resync timeout, `handle_resync_ready` late-ack path resumes the slot on the host but does **not** `broadcast_slot_ai_resume` / catch-up snapshot to bystanders — host treats the slot as human while peers keep AI (draft fix PR **#75** — not merged; **#83** also reworks this late path) |
| Duplicate multi-peer `ResyncReady` | When the slot is already out of `resync_pending_slots_mask_`, the multi-peer branch still calls `reset_reconnect_handshake_sync_state`, wiping pipelined host batches (draft fix PR **#83** — not merged) |
| Pause batch pipelining | `can_pipeline_batches` ignores `match_paused_`, so a paused peer can send ahead while others advance (draft fix PR **#83** — not merged) |
| Reconnect command double-apply | Snapshot pending log restores commands, then live batches re-enqueue the same `(player_slot, sequence)` with no dedupe in `CommandQueue::enqueue_network` (draft fix PR **#83** — not merged) |
| AI during resync handshake | `inject_ai_commands_for_disconnected_slots` ORs `resync_pending_slots_mask_` into the AI-playing mask, so the host injects AI for a slot still waiting on `ResyncReady` (draft fix PR **#83** — not merged) |
| Parallel `AdvanceAge` | `issue_advance_age_order` only blocks an active process on the chosen Town Center; two TCs can finish age-up on the same tick and skip a tier cost (draft fix PR **#83** — not merged) |
| `PlayerResign` / `ResyncReady` sender auth | Host trusts payload `player_slot` without binding it to the ENet sender (lobby already checks this). Draft fix PR **#79** — not merged |
| Post-resign local input | Resign excludes the slot from the remote-ready mask while local submit / non-`Resign` outbox can still fire (draft fix PR **#79** — not merged) |
| `TickInputBatch` slot impersonation | Host does not require `batch.player_slot` to match the sender ENet slot; accepts forged batches and rebinds the claimed slot (draft fix PR **#81** — not merged) |
| Host double-AI on multi-peer drop | `note_player_slot_transport_down` enables both disconnect-inject AI and `ai_controlled_slots_mask_` prepare AI → host runs AI twice per tick vs clients once (draft fix PR **#81** — not merged) |
| Resign then reconnect drops match outcome | Snapshot v26 does not encode `eliminated_slots_mask` / `match_finished` / `winner_slot` / `last_eliminating_slot` / `finished_tick`, and `compute_state_hash` does not mix them — silent desync after resign + mid-match rejoin (draft fix PR **#85** — not merged; proposes snapshot v27 + `--snapshot-resign-smoke`) |
| `SlotAiTakeover` / `SlotAiResume` sender auth | Host and clients apply payload `player_slot` from any sender; forged takeover marks a live opponent AI on one peer only (draft fix PR **#87** — not merged) |
| Graphical lockstep registry race | UI thread `handle_event` / `update_continuous` / `make_hud_context` touch the live registry without `simulation_access_mutex` while the background tick loop mutates it (draft fix PR **#87** — not merged) |
| Stale per-slot AI sequences after snapshot | `sync_command_sequences_from_input_log` rebuilds global `local_command_sequence_` / `ai_command_sequence_` from the restored input log but leaves `ai_command_sequence_by_slot_[]` stale. Post-reconnect `inject_ai_commands_for_disconnected_slots` can reuse sequences already in the pending log → desync (draft fix PR **#93** — not merged) |
| Bystander catch-up snapshot → spurious handshake | `handle_reconnect_snapshot` ORs `state_changed` into the handshake condition. Live peers receiving a host catch-up after another slot reconnects always see tick/hash advance, so they reset sync state and send `ResyncReady` (draft fix PR **#95** — not merged) |
| CI smoke exit codes | PowerShell Headless smoke step does not fail the job on native non-zero exits |
| Missing pack file breaks CI | `SFX_LOOK_HERE_RELATIVE_PATH` → `sfx/Cringemarine/look-here.wav` is referenced but not in `assets/`; `aoa_pack_assets` fails POST_BUILD (docs PRs inherit this until the asset lands) |
| Resign vs disconnect | Resign / leave marks the slot and skips AI; unexpected drop still takes AI |

## Related

- [BUILD.md](BUILD.md) — presets and short CLI table
- [HARNESS.md](HARNESS.md) — scenario hashes and command replay
- [LAN_SOAK.md](LAN_SOAK.md) — two-PC soak checklist
- [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md) — 4–8 player scale plan
- [ECS.md](ECS.md) — tick pipeline after commands apply
- [DECISIONS.md](DECISIONS.md) — command timing, disconnect policy
- [RELEASE_NOTES.md](../RELEASE_NOTES.md) — alpha_v0.2.1 resign / reconnect notes
