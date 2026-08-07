## Networking test scenarios

Epic from [docs/BACKLOG.md](../../docs/BACKLOG.md) — M2.

### Automated (CI / daily dev loop)

- [x] Two local instances (localhost) — `--lockstep-host` / `--lockstep-join`
- [x] Headless lockstep hash match — `aoa --lockstep-smoke`
- [x] Snapshot encode/decode — `aoa --snapshot-smoke`
- [x] Host continues sim immediately when client drops — `aoa --lockstep-disconnect-smoke`
- [x] Three disconnect/reconnect cycles in-process — `aoa --lockstep-reconnect-smoke`

### LAN soak (manual — required before M2 done)

Run on **two physical machines** on the same LAN (not localhost). Default port `27000`. Enable debug logs: `--lockstep-debug` → `logs/lockstep_p1_host.log`, `logs/lockstep_p2_client.log`.

#### 2-player LAN

- [x] Host + join; play 30+ minutes with gather, move, attack
- [x] Kill client process mid-match → host title shows **Player 2 left — AI playing**; sim **does not freeze**
- [x] Restart client within 30s → reconnect, snapshot resync, human control restored
- [x] Repeat disconnect/reconnect 3× without desync
- [x] Drag host window / alt-tab both clients — no permanent stall (network thread)

**Deferred (not M2 blocking):** combat attack pathfinding dog-legs — [pathfinding-combat.md](pathfinding-combat.md)

#### 4-player LAN

*Requires 8-player lockstep scaling (M3 transport). Run once multi-peer host ships.*

- [ ] Four machines (or 2 machines × 2 processes each on different ports if supported)
- [ ] All four ready-check; sim advances with no global pause on single drop
- [ ] One player disconnect → AI for that slot only; others unaffected
- [ ] Reconnect one player while three remain in match
- [ ] 20+ minute session; no desync; FPS stays near target on all clients



#### 8-player brutal test

*Full 8-human (or 8-client) stress test — **M2 exit criterion** for multiplayer feel at target scale. Depends on M3 multi-peer lockstep; run as soon as 8-way sync lands.*

**Setup**

- 8 clients (mix of LAN machines + headless bots acceptable for soak, but at least **4 real graphical clients** on separate machines)
- Same map, 20 tps, `LOCKSTEP_COMMAND_DELAY_TICKS` as shipped
- Optional: artificial latency (50–150 ms) and 1–5% packet loss on one client (Linux `tc netem` or similar)

**Brutal checklist**

- [ ] 60+ minute continuous match without desync
- [ ] Simultaneous disconnect of 2 non-host players → AI for both; sim never freezes
- [ ] Staggered reconnect of both within grace window
- [ ] One “bad connection” client (loss/latency) — brief “syncing” stalls only; **no whole-match freeze**
- [ ] Host alt-tab / window drag during peak unit count — tick loop keeps 20 tps average
- [ ] All 8 clients report smooth unit motion (interpolation; no “teleport every tick” feel)
- [ ] CPU and upstream bandwidth per client documented (baseline for M3 perf budget)

**Pass criteria**

- Zero desyncs in brutal run
- No session-wide freeze > 2s except initial join/resync handshake
- Reconnect success rate 100% within grace period in scripted disconnect script
- Subjective: “feels close to singleplayer” on good LAN; acceptable on degraded client



### Before closing M2

- [x] 2-player LAN checklist complete
- [x] Issue bodies (`m2-lockstep.md`, `m2-reconnect.md`, `m2-transport.md`) match implemented behavior
- [ ] 4-player and 8-player LAN rows scheduled immediately after multi-peer lockstep (M3) merges