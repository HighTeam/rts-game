# LAN soak prep (M2 exit test)

Two physical Windows PCs on the same LAN. Default game port: **27000**.

## Before you start

1. Build **Release** on both machines from the same commit:
   ```powershell
   cmake --build build\x64-release --config Release
   ```
2. Allow **aoa.exe** through Windows Firewall on the **host** (private network).
3. Find the host PC LAN IP (`ipconfig` → IPv4, e.g. `192.168.1.42`).
4. Optional: create a `logs` folder next to `aoa.exe` for lockstep debug output.

## Launch commands

**Machine A — host (Player 1)**

```powershell
.\build\x64-release\Release\aoa.exe --lockstep-host --port 27000 --lockstep-debug
```

Or use the helper script:

```powershell
.\scripts\run-lan-host.ps1
```

**Machine B — client (Player 2)**

Replace `HOST_IP` with the host's LAN address:

```powershell
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --lockstep-debug
```

Or:

```powershell
.\scripts\run-lan-join.ps1 -HostAddress 192.168.1.42
```

With `--lockstep-debug`, logs are written under `logs/` (e.g. `lockstep_p1_host.log`, `lockstep_p2_client.log`).

## 30-minute checklist

- [ ] Both clients show **FPS ~59–60** and **ping ~10–25 ms** on LAN
- [ ] Gather wood, move units, attack — no desync banner
- [ ] Kill P2 process → P1 title: **Player 2 left — AI playing**; sim keeps running
- [ ] Restart P2 within 30 s → reconnect completes; human control restored
- [ ] Repeat disconnect/reconnect **3×** without desync
- [ ] Alt-tab / drag window on both PCs — no permanent stall

## If something fails

| Symptom | Check |
|--------|--------|
| Client cannot connect | Firewall, correct IP, host already listening |
| Stuck after reconnect | Host/client `logs/lockstep_*.log` for `resync_ready_received`, `reconnect_bootstrap_complete` |
| Desync | Both logs at desync tick; compare `tick=` and hash lines |
| Movement stutter only | Usually render/interpolation; note if it happens only while waiting on opponent batches |

## Automated smokes (same build, single machine)

```powershell
.\build\x64-release\Release\aoa.exe --harness
.\build\x64-release\Release\aoa.exe --lockstep-smoke
.\build\x64-release\Release\aoa.exe --lockstep-disconnect-smoke
.\build\x64-release\Release\aoa.exe --lockstep-reconnect-smoke
.\build\x64-release\Release\aoa.exe --lockstep-4-smoke
.\build\x64-release\Release\aoa.exe --lockstep-4-disconnect-smoke
.\build\x64-release\Release\aoa.exe --lockstep-peer-silence-smoke
```

Pass these before spending time on the LAN soak.
