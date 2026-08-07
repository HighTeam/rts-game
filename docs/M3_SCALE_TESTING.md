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

After M3 multi-peer ships, add headless smokes (same pattern as `--lockstep-reconnect-smoke`):

| Smoke | Purpose |
|-------|---------|
| `--lockstep-4-smoke` | 4 peers in-process or 4 localhost processes; hash match + basic commands |
| `--lockstep-8-smoke` | 8 peers; staggered join, one disconnect + AI + reconnect |
| `--lockstep-8-disconnect-smoke` | Two non-host slots drop; AI for both; sim never freezes |

Implementation options (pick one for M3):

1. **In-process mesh** — one test binary runs host + N−1 virtual `LockstepSession` clients (fastest CI, like today’s 2-player smoke).
2. **Multi-process localhost** — test runner spawns `aoa --lockstep-join 127.0.0.1:PORT --headless --slot N` (closer to real ENet, slower CI).

Both must pass before calling N-player sync “proven.”

---

## Single PC, 8 players (dev soak)

One Windows box, Release build:

```
[Host P1]  --lockstep-host --port 27000
[P2–P8]  --lockstep-join 127.0.0.1:27000 --headless --player-slot N
```

Requires M3:

- Host accepts **7** ENet peers (not 1).
- `--player-slot` (or join handshake assigns slot 1–7).
- Optional `--lockstep-bot` — headless client that only ACKs batches (no window); used to fill empty slots in soak scripts.

Helper script (M3): `scripts/run-scale-soak-localhost.ps1 -Players 8 -Minutes 60`

Graphical client optional (only P1 or P1+P2 with windows); P3–P8 headless is enough for **sync** testing.

---

## Two PCs, up to 8 players (your setup)

Split headless clients across two LAN machines. **One host** on PC A; all joiners use PC A’s LAN IP.

### 8-player layout

| Machine | Processes | Role |
|---------|-----------|------|
| **PC A** | 1× graphical host | Player 1, port 27000 |
| **PC A** | 3× headless join | Players 2–4 |
| **PC B** | 4× headless join | Players 5–8 |

Commands (after M3):

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

Firewall: allow **27000** on PC A from PC B. Each headless process is a real ENet client — same code path as human players.

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

1. Multi-peer ENet host + slot assignment in join handshake  
2. N-way input batch gating (all slots ready before tick)  
3. `--lockstep-4-smoke` / `--lockstep-8-smoke` in CI  
4. Headless multi-join CLI (`--player-slot`, soak scripts)  
5. 2-PC scripted soak (`scripts/run-scale-soak-lan.ps1`)  
6. Brutal checklist in [m2-tests.md](../scripts/issue-bodies/m2-tests.md) § 8-player — run once 8-smoke green + one 2-PC split soak  

---

## Pass criteria (8-player “proven”)

- CI: `--lockstep-8-smoke` and disconnect variant green on `ci-x64-debug` + `ci-x64-release`
- Local: 60+ min localhost script with 7 headless + 1 host, zero desyncs
- LAN: 30+ min with 2-PC split layout above, at least 2 graphical clients, reconnect once for a non-host slot
- Deferred: full “4 real graphical clients on 4 machines” — optional; not required for solo dev with 2 PCs
