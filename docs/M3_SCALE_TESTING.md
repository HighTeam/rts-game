# M3 scale testing (4–8 players, two PCs)

M2 proved **2-player** lockstep on LAN. M3 adds **multi-peer transport** (`MAX_PEERS` > 2, N-way input gating). You do **not** need 4–8 physical machines to validate scale.

## What we test where

| Layer | Where | Needs extra PCs? |
|-------|--------|-------------------|
| **Determinism / desync** | CI headless smokes (`--lockstep-N-smoke`) | No |
| **Reconnect / AI / snapshot** | Same smokes + existing 2-player LAN | No (CI) / 2 PCs (LAN spot-check) |
| **Subjective feel** (FPS, interpolation) | 1–2 graphical clients | 2 PCs enough |
| **Long soak (60+ min)** | Script spawning headless peers | No (1 PC) or split (2 PCs) |

**Rule:** Automate N-player sync on **one machine in CI**. Use **two PCs** only for a short LAN spot-check (firewall, real RTT, one graphical client per machine).

---

## CI / daily dev (no LAN)

| Smoke | Status | Purpose |
|-------|--------|---------|
| `--lockstep-4-smoke` | **Shipped** (PR #38, in CI) | 4 peers in-process; empty batches; compares local `Simulation::state_hash()` after 40 ticks (not wire desync detection) |
| `--lockstep-4-disconnect-smoke` | Open fix #59 (not on `main`) | Disconnect a non-P2 client; remaining peers stay hash-synced |
| `--lockstep-8-smoke` | Not implemented | 8 peers; staggered join, one disconnect + AI + reconnect |
| `--lockstep-8-disconnect-smoke` | Not implemented | Two non-host slots drop; AI for both; sim never freezes |

`--lockstep-4-smoke` uses the in-process mesh path (host + 3 `LockstepSession` clients). Multi-process localhost with `--player-slot` is still the planned soak shape, not a live CLI flag.

Graphical `--lockstep-host` / `--lockstep-join` remain **2-player** (`LOCKSTEP_PLAYER_COUNT`). N-way gating exists inside `LockstepSession`; daily play does not open 7 client slots yet. Disconnect AI still keys off the 2-player opponent helper (`opponent_player_slot()`, always slot 1 for the host) and flips a global `ai_fallback_` that leaves remaining peers in live gating against a divergent host (open fix #59). Mid-match reconnect in a 4-peer session is **not** proven: snapshot targeting, JoinAccepted game-slot→ENet targeting, host hash fan-out, missing TickStateHash relay, and the global resync batch gate can freeze or mis-route peers. Details: [LOCKSTEP.md](LOCKSTEP.md).

---

## Single PC, 8 players (dev soak)

Planned shape once host max-clients and slot CLI land:

```
[Host P1]  --lockstep-host --port 27000
[P2–P8]  --lockstep-join 127.0.0.1:27000 --headless --player-slot N
```

Still required before this works end-to-end:

- Graphical/headless host accepts **7** ENet peers (today: 1 client).
- `--player-slot` CLI (not implemented; unknown args throw).
- Optional `--lockstep-bot` — headless client that only ACKs batches.
- Helper script: `scripts/run-scale-soak-localhost.ps1` (not in tree yet).

Until then, use `--lockstep-4-smoke` for N-way sync and [LAN_SOAK.md](LAN_SOAK.md) for 2-player feel.

---

## Two PCs, up to 8 players (your setup)

Split headless clients across two LAN machines. **One host** on PC A; all joiners use PC A’s LAN IP.

### 8-player layout

| Machine | Processes | Role |
|---------|-----------|------|
| **PC A** | 1× graphical host | Player 1, port 27000 |
| **PC A** | 3× headless join | Players 2–4 |
| **PC B** | 4× headless join | Players 5–8 |

Commands (planned; `--player-slot` and multi-client host are not live yet):

```powershell
# PC A — host
.\aoa.exe --lockstep-host --port 27000 --lockstep-debug

# PC A — local headless (3 windows or scripted)
.\aoa.exe --lockstep-join 127.0.0.1:27000 --headless --player-slot 2
.\aoa.exe --lockstep-join 127.0.0.1:27000 --headless --player-slot 3
.\aoa.exe --lockstep-join 127.0.0.1:27000 --headless --player-slot 4

# PC B — remote headless (HOST_IP = PC A)
.\aoa.exe --lockstep-join HOST_IP:27000 --headless --player-slot 5
# … slots 6–8
```

Firewall: allow **27000** on PC A from PC B. Each headless process is a real ENet client — same code path as human players. Today, stick to 2-player [LAN_SOAK.md](LAN_SOAK.md) plus `--lockstep-4-smoke`.

### 4-player layout (lighter)

| PC A | Host + 1 headless join (P2) |
| PC B | Graphical join (P3) + 1 headless (P4) |

One graphical client per PC preserves “does it feel good on LAN” without four monitors.

---

## What 2-PC LAN cannot replace

- **CI smokes** — must run on every push (already in `.github/workflows/build.yml`).
- **8-way edge cases** — simultaneous double-disconnect, bad link on slot 7: script on **one** PC with optional `tc netem`-style latency injection later; not all scenarios need two homes.

Two-PC LAN confirms: routing, firewall, non-zero RTT, reconnect across machines. Not a substitute for `--lockstep-8-smoke` in CI.

---

## M3 implementation order (testing-aware)

1. ~~Multi-peer ENet host capacity + N-way input batch gating~~ (session supports up to 8; 4-smoke uses it)
2. ~~`--lockstep-4-smoke` in CI~~
3. Wire graphical/headless host to `session_player_count > 2` and assign join slots
4. `--lockstep-8-smoke` (+ disconnect variant) in CI
5. Headless multi-join CLI (`--player-slot`, soak scripts)
6. 2-PC scripted soak (`scripts/run-scale-soak-lan.ps1`)
7. Brutal checklist in [m2-tests.md](../scripts/issue-bodies/m2-tests.md) § 8-player — run once 8-smoke green + one 2-PC split soak

---

## Pass criteria (8-player “proven”)

- CI: `--lockstep-8-smoke` and disconnect variant green on `ci-x64-debug` + `ci-x64-release`
- Local: 60+ min localhost script with 7 headless + 1 host, zero desyncs
- LAN: 30+ min with 2-PC split layout above, at least 2 graphical clients, reconnect once for a non-host slot
- Deferred: full “4 real graphical clients on 4 machines” — optional; not required for solo dev with 2 PCs
