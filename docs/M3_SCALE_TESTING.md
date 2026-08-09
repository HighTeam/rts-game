# 4–8 player testing instructions

You do **not** need 4–8 physical PCs. Use **automated smokes** on one machine for sync proof, and **two PCs** only for a short LAN spot-check (firewall + real ping).

Default game port: **27000**. Build path below assumes Release at `build\x64-release\Release\aoa.exe`.

---

## 0. Build (both machines, same commit)

```powershell
cd D:\Projects\rts-game
cmake --build build\x64-release --config Release
```

Optional: tag or note commit hash so host and join PCs match (e.g. `git checkout m2` for M2 baseline, or `main` after M3 merge).

---

## 1. Automated smokes — run first (one PC, ~1 min)

From repo root, in order:

```powershell
$exe = ".\build\x64-release\Release\aoa.exe"

& $exe --harness
& $exe --lockstep-smoke
& $exe --lockstep-disconnect-smoke
Start-Sleep -Milliseconds 500
& $exe --lockstep-reconnect-smoke
Start-Sleep -Milliseconds 500
& $exe --lockstep-4-smoke
```

**Pass criteria**

| Command | Expected |
|---------|----------|
| `--harness` | `All scenarios passed` |
| `--lockstep-smoke` | `ok ticks=40 hash=0x...` |
| `--lockstep-disconnect-smoke` | `ok ai_at_tick=... continued_to=...` |
| `--lockstep-reconnect-smoke` | `ok cycles=3 ticks=130 hash=0x...` |
| `--lockstep-4-smoke` | `ok ticks=40 hash=0x74adf4a4c1592f59` (hash must match across 4 peers) |

If any fail, fix before LAN or long soaks.

---

## 2. Four-player sync (available today)

### 2a. Headless CI-style (what you already ran)

```powershell
.\build\x64-release\Release\aoa.exe --lockstep-4-smoke
```

One process runs host (slot 0) + three in-process clients (slots 1–3). No windows. This is the **authoritative 4-player desync check** until manual multi-join CLI lands.

### 2b. Graphical / multi-process 4-player

Use **`--players 4`** (alias: `--players 4`) on the host and every join. Each slot gets a town center, worker, and militia on a **4-corner map**. The host shows **Waiting for players (N/4)** until all clients connect; the sim does not advance until the lobby is full.

Connect in order: **P2 → P3 → P4** (ENet slot must match `--player-slot`).

---

## 3. Two-player LAN (graphical — your two PCs)

Full checklist and portable copy steps: [LAN_SOAK.md](LAN_SOAK.md) **§2–§4**.

**Recommended:** stage `D:\aoa-lan` on the build PC (exe + DLLs + `data\` + `assets\`), copy that folder to the second PC, then run with explicit flags only.

**PC A (host)** — start first:

```powershell
cd D:\aoa-lan
.\aoa.exe --lockstep-host --port 27000 --lockstep-debug
```

**PC B (client)** — replace `192.168.x.x` with PC A’s LAN IP (`ipconfig`):

```powershell
cd D:\aoa-lan
.\aoa.exe --lockstep-join 192.168.x.x:27000 --lockstep-debug
```

Logs (with `--lockstep-debug`): `D:\aoa-lan\logs\lockstep_p1_host.log`, `lockstep_p2_client.log`.

**Repo-only optional helpers** (both machines have the full checkout): `.\scripts\run-lan-host.ps1` / `.\scripts\run-lan-join.ps1` — see [LAN_SOAK.md](LAN_SOAK.md) §5.

**Checklist:** 30+ min play, disconnect/reconnect, alt-tab — see [LAN_SOAK.md](LAN_SOAK.md) §6.

---

## 4. Four-player on two PCs

Goal: **sync** without four monitors. One graphical host; other slots headless.

| PC | Processes | Slots |
|----|-----------|-------|
| **A** | 1× graphical host | P1 |
| **A** | 1× headless join | P2 |
| **B** | 1× graphical join | P3 |
| **B** | 1× headless join | P4 |

**PC A** — allow port **27000** in Windows Firewall (private network).

```powershell
# Terminal 1 — host
.\build\x64-release\Release\aoa.exe --lockstep-host --port 27000 --players 4 --lockstep-debug

# Terminal 2 — local headless P2 (connect after host is listening)
.\build\x64-release\Release\aoa.exe --lockstep-join 127.0.0.1:27000 --players 4 --headless --player-slot 2 --lockstep-debug
```

**PC B** — `HOST_IP` = PC A’s IPv4:

```powershell
# Terminal 1 — graphical P3
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --players 4 --player-slot 3 --lockstep-debug

# Terminal 2 — headless P4
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --players 4 --headless --player-slot 4 --lockstep-debug
```

**Connect order:** start host → join P2 → wait for `lockstep-join: joined` → join P3 → join P4 (one client at a time, slots 2→3→4).

**Pass:** all four advance ticks; no desync banner; gather/move on P1 and P3; 20+ minutes stable.

Every join process must pass the same `--players 4` as the host.

---

## 5. Eight-player on two PCs

| PC | Processes | Slots |
|----|-----------|-------|
| **A** | 1× graphical host | P1 |
| **A** | 3× headless join | P2, P3, P4 |
| **B** | 4× headless join | P5, P6, P7, P8 |

Optional: run **one** graphical client on PC B (e.g. P5) instead of headless for feel testing.

**PC A**

```powershell
.\build\x64-release\Release\aoa.exe --lockstep-host --port 27000 --players 8 --lockstep-debug

.\build\x64-release\Release\aoa.exe --lockstep-join 127.0.0.1:27000 --players 8 --headless --player-slot 2 --lockstep-debug
.\build\x64-release\Release\aoa.exe --lockstep-join 127.0.0.1:27000 --players 8 --headless --player-slot 3 --lockstep-debug
.\build\x64-release\Release\aoa.exe --lockstep-join 127.0.0.1:27000 --players 8 --headless --player-slot 4 --lockstep-debug
```

**PC B** — `HOST_IP` = PC A’s IPv4:

```powershell
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --players 8 --headless --player-slot 5 --lockstep-debug
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --players 8 --headless --player-slot 6 --lockstep-debug
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --players 8 --headless --player-slot 7 --lockstep-debug
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --players 8 --headless --player-slot 8 --lockstep-debug
```

**Automated 8-player sync (planned):**

```powershell
.\build\x64-release\Release\aoa.exe --lockstep-8-smoke
```

**Single-PC 8-player soak (planned):** seven headless joins to `127.0.0.1:27000` + one host; script `scripts/run-scale-soak-localhost.ps1 -Players 8 -Minutes 60`.

---

## 6. Eight-player “brutal” checklist (after 8-smoke green)

From [m2-tests.md](../scripts/issue-bodies/m2-tests.md) § 8-player:

- 60+ min continuous match, zero desyncs
- Two non-host disconnects → AI for both, sim never freezes
- Staggered reconnect of both
- One degraded client (optional latency/loss) — brief stalls only, no whole-match freeze
- Document CPU/bandwidth per client

Run brutal **localhost + headless** first; then one **2-PC split soak** (§5) with 2 graphical clients.

---

## 7. What to use when (quick reference)

| Goal | Method | PCs |
|------|--------|-----|
| Daily dev / CI | §1 smokes | 1 |
| Prove 4-way sync | `--lockstep-4-smoke` | 1 |
| Prove 8-way sync | `--lockstep-8-smoke` (planned) | 1 |
| Feel + LAN ping | §3 two-player graphical | 2 |
| 4-player LAN sync | §4 | 2 |
| 8-player LAN sync | §5 | 2 |
| 60+ min stress | localhost headless soak script (planned) | 1 |

---

## 8. Troubleshooting

| Symptom | Check |
|---------|--------|
| Client cannot connect | Firewall on host, correct LAN IP, host listening first |
| Only one “lockstep-join: joined” in 4-smoke | Re-run; ports 27202 must be free |
| Desync | `logs/lockstep_*.log` at desync tick; compare hashes |
| 4-smoke ok but LAN bad | Connect in slot order (P2→P3→P4); same `--lockstep-players` on every process |
| Hash mismatch after sim change | Re-run smokes; update harness JSON if intentional |

See also [BUILD.md](BUILD.md) for all CLI flags and [LAN_SOAK.md](LAN_SOAK.md) for M2 two-player soak.
