# LAN soak prep (M2 exit test)

Two physical Windows PCs on the same LAN. Default game port: **27000**.

Use the **portable release folder** below when testing on a second PC. Copy only the Release output — no repo, no CMake, no `build\` tree. The helper scripts under `scripts/` are optional conveniences when both PCs have the full repo checked out.

---

## 1. Build Release (machine with the repo)

Same commit on both PCs if you build twice; otherwise build once and copy the staged folder.

```powershell
cd D:\Projects\rts-game
cmake --build build\x64-release --config Release
```

Source folder (after build):

`D:\Projects\rts-game\build\x64-release\Release\`

---

## 2. Stage portable folder (copy only what the exe needs)

Run on the machine that built Release. Adjust `$Stage` if you want another path (e.g. USB stick).

```powershell
$Repo   = "D:\Projects\rts-game"
$Src    = Join-Path $Repo "build\x64-release\Release"
$Stage  = "D:\aoa-lan"

New-Item -ItemType Directory -Force -Path $Stage | Out-Null

Copy-Item -Force (Join-Path $Src "aoa.exe")              $Stage
Copy-Item -Force (Join-Path $Src "sfml-system-3.dll")    $Stage
Copy-Item -Force (Join-Path $Src "sfml-window-3.dll")    $Stage
Copy-Item -Recurse -Force (Join-Path $Src "data")        $Stage
Copy-Item -Recurse -Force (Join-Path $Src "assets")       $Stage
```

**Ship these five items** (same layout on both PCs):

| Path (under `$Stage`) | Required |
|----------------------|----------|
| `aoa.exe` | yes |
| `sfml-system-3.dll` | yes |
| `sfml-window-3.dll` | yes |
| `data\` | yes |
| `assets\` | yes |

Do **not** copy the repo, `build\`, `raw-assets\`, or CMake presets. `logs\` is created at runtime when you pass `--lockstep-debug`.

Copy the whole `$Stage` folder to the second PC (ZIP, USB, network share). On PC B, use the same folder name/path or any path — only the contents matter.

---

## 3. Before LAN play

1. Allow **aoa.exe** through Windows Firewall on the **host** (private network).
2. On the host PC, get the LAN IPv4 address:

```powershell
ipconfig
```

Use the adapter that is on your home/LAN network (e.g. `192.168.1.42`). Below it is written as `HOST_IP`.

3. Optional: create an empty `logs` folder next to `aoa.exe` if you want logs ready before launch:

```powershell
New-Item -ItemType Directory -Force -Path "D:\aoa-lan\logs" | Out-Null
```

---

## 4. Launch — full command line (portable folder)

**PC A — host (player 1)** — start first:

```powershell
cd D:\aoa-lan
.\aoa.exe --lockstep-host --port 27000 --lockstep-debug
```

**PC B — client (player 2)** — replace `HOST_IP`:

```powershell
cd D:\aoa-lan
.\aoa.exe --lockstep-join HOST_IP:27000 --lockstep-debug
```

Example with a real address:

```powershell
cd D:\aoa-lan
.\aoa.exe --lockstep-join 192.168.1.42:27000 --lockstep-debug
```

**Headless** (scripted tick limit, no window) — same folder, explicit flags:

```powershell
cd D:\aoa-lan
.\aoa.exe --lockstep-host --port 27000 --headless --ticks 100
```

```powershell
cd D:\aoa-lan
.\aoa.exe --lockstep-join HOST_IP:27000 --headless --ticks 100
```

With `--lockstep-debug`, logs are written under `D:\aoa-lan\logs\` (e.g. `lockstep_p1_host.log`, `lockstep_p2_client.log`).

---

## 5. Repo dev helpers (optional — both PCs have the repo)

If you are developing on both machines with `D:\Projects\rts-game` checked out, you can use:

```powershell
cd D:\Projects\rts-game
.\scripts\run-lan-host.ps1 -Port 27000 -Debug
```

```powershell
cd D:\Projects\rts-game
.\scripts\run-lan-join.ps1 -HostAddress HOST_IP -Port 27000 -Debug
```

Those scripts still run `build\x64-release\Release\aoa.exe` from the repo. For a second PC without the repo, use **§2–§4** instead.

Equivalent explicit commands from repo root (no script):

```powershell
cd D:\Projects\rts-game
.\build\x64-release\Release\aoa.exe --lockstep-host --port 27000 --lockstep-debug
```

```powershell
cd D:\Projects\rts-game
.\build\x64-release\Release\aoa.exe --lockstep-join HOST_IP:27000 --lockstep-debug
```

---

## 6. 30-minute checklist

- [ ] Both clients show **FPS ~59–60** and **ping ~10–25 ms** on LAN
- [ ] Gather wood, move units, attack — no desync banner
- [ ] Kill P2 process → P1 title: **Player 2 left — AI playing**; sim keeps running
- [ ] Restart P2 within 30 s → reconnect completes; human control restored
- [ ] Repeat disconnect/reconnect **3×** without desync
- [ ] Alt-tab / drag window on both PCs — no permanent stall

---

## 7. If something fails

| Symptom | Check |
|--------|--------|
| Client cannot connect | Firewall, correct `HOST_IP`, host already listening on `--port 27000` |
| Missing DLL / assets | Re-run **§2**; confirm `data\`, `assets\`, and both SFML DLLs sit next to `aoa.exe` |
| Stuck after reconnect | Host/client `logs\lockstep_*.log` for `resync_ready_received`, `reconnect_bootstrap_complete` |
| Desync | Both logs at desync tick; compare `tick=` and hash lines |
| Movement stutter only | Usually render/interpolation; note if it happens only while waiting on opponent batches |

---

## 8. Automated smokes (same build, single machine)

Run from repo (not required on the second PC):

```powershell
cd D:\Projects\rts-game
.\build\x64-release\Release\aoa.exe --harness
.\build\x64-release\Release\aoa.exe --lockstep-smoke
.\build\x64-release\Release\aoa.exe --lockstep-disconnect-smoke
.\build\x64-release\Release\aoa.exe --lockstep-reconnect-smoke
.\build\x64-release\Release\aoa.exe --lockstep-4-smoke
```

Pass these before spending time on the LAN soak.

Scale testing (4–8 players, two-PC layouts): [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md)
