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
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "scenarios") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "patterns") | Out-Null

Copy-Item -Force (Join-Path $Src "AgeofAffinities.exe") $Stage
Copy-Item -Force (Join-Path $Src "assets.dat")          $Stage
Get-ChildItem -LiteralPath $Src -Filter "*.dll" | ForEach-Object {
    Copy-Item -Force $_.FullName $Stage
}
Copy-Item -Recurse -Force (Join-Path $Src "scenarios\*") (Join-Path $Stage "scenarios")
Copy-Item -Recurse -Force (Join-Path $Src "patterns\*")  (Join-Path $Stage "patterns")
```

**Ship these items** (same layout on both PCs):

| Path (under `$Stage`) | Required |
|----------------------|----------|
| `AgeofAffinities.exe` | yes |
| `assets.dat` | yes (POST_BUILD pack; includes former `assets/` + `data/`) |
| `*.dll` | yes (all SFML/runtime DLLs next to the exe) |
| `scenarios\` | yes |
| `patterns\` | yes |

Do **not** copy loose `assets\` / `data\` trees, the repo, `build\`, or `raw-assets\`. Release POST_BUILD removes those loose folders from the output. `logs\` is created at runtime when you pass `--lockstep-debug`.

Copy the whole `$Stage` folder to the second PC (ZIP, USB, network share). On PC B, use the same folder name/path or any path — only the contents matter.

---

## 3. Before LAN play

1. Allow **AgeofAffinities.exe** through Windows Firewall on the **host** (private network).
2. On the host PC, get the LAN IPv4 address:

```powershell
ipconfig
```

Use the adapter that is on your home/LAN network (e.g. `192.168.1.42`). Below it is written as `HOST_IP`.

3. Optional: create an empty `logs` folder next to `AgeofAffinities.exe` if you want logs ready before launch:

```powershell
New-Item -ItemType Directory -Force -Path "D:\aoa-lan\logs" | Out-Null
```

---

## 4. Launch — full command line (portable folder)

**PC A — host (player 1)** — start first:

```powershell
cd D:\aoa-lan
.\AgeofAffinities.exe --lockstep-host --port 27000 --lockstep-debug
```

**PC B — client (player 2)** — replace `HOST_IP`:

```powershell
cd D:\aoa-lan
.\AgeofAffinities.exe --lockstep-join HOST_IP:27000 --lockstep-debug
```

Example with a real address:

```powershell
cd D:\aoa-lan
.\AgeofAffinities.exe --lockstep-join 192.168.1.42:27000 --lockstep-debug
```

**Headless** (scripted tick limit, no window) — same folder, explicit flags:

```powershell
cd D:\aoa-lan
.\AgeofAffinities.exe --lockstep-host --port 27000 --headless --ticks 100
```

```powershell
cd D:\aoa-lan
.\AgeofAffinities.exe --lockstep-join HOST_IP:27000 --headless --ticks 100
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

Those scripts still run `build\x64-release\Release\AgeofAffinities.exe` from the repo. For a second PC without the repo, use **§2–§4** instead.

Equivalent explicit commands from repo root (no script):

```powershell
cd D:\Projects\rts-game
.\build\x64-release\Release\AgeofAffinities.exe --lockstep-host --port 27000 --lockstep-debug
```

```powershell
cd D:\Projects\rts-game
.\build\x64-release\Release\AgeofAffinities.exe --lockstep-join HOST_IP:27000 --lockstep-debug
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
| Missing DLL / assets | Re-run **§2**; confirm `assets.dat`, `scenarios\`, `patterns\`, and all `*.dll` sit next to `AgeofAffinities.exe` |
| Stuck after reconnect | Host/client `logs\lockstep_*.log` for `resync_ready_received`, `reconnect_bootstrap_complete` |
| Desync | Both logs at desync tick; compare `tick=` and hash lines |
| Movement stutter only | Usually render/interpolation; note if it happens only while waiting on opponent batches |

---

## 8. Automated smokes (same build, single machine)

Run from repo (not required on the second PC):

```powershell
cd D:\Projects\rts-game
$exe = ".\build\x64-release\Release\AgeofAffinities.exe"
& $exe --harness
& $exe --lockstep-smoke
& $exe --lockstep-disconnect-smoke
& $exe --lockstep-reconnect-smoke
& $exe --lockstep-4-smoke
& $exe --lockstep-4-disconnect-smoke
& $exe --lockstep-4-reconnect-smoke
```

Pass these before spending time on the LAN soak. Full port table and caveats: [LOCKSTEP.md](LOCKSTEP.md).

Scale testing (4–8 players, two-PC layouts): [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md)
