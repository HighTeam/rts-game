# Build guide

Age of Affinities targets **Windows 10/11 x64 only** — Debug and Release. There is no x86 (32-bit) build; modern desktop RTS players are on 64-bit Windows, and maintaining a second architecture doubles CI time and vcpkg work for no practical gain.

## Prerequisites

| Tool | Notes |
|------|-------|
| Visual Studio 2022 | Desktop development with C++ workload |
| CMake 3.24+ | `cmake --version` |
| Git | For vcpkg bootstrap |

## First-time setup

From the repo root:

```powershell
.\scripts\configure.ps1
```

This script:

1. Clones [vcpkg](https://github.com/microsoft/vcpkg) into `.tools/vcpkg` (gitignored, pinned commit)
2. Sets `VCPKG_ROOT` for the session
3. Configures and builds **x64 Debug** and **x64 Release** via [CMakePresets.json](../CMakePresets.json)

Dependencies come from [vcpkg.json](../vcpkg.json): SFML, glad, EnTT, ENet, nlohmann-json.

## Daily commands

```powershell
. .\scripts\bootstrap.ps1

cmake --preset x64-debug
cmake --build --preset x64-debug

cmake --preset x64-release
cmake --build --preset x64-release
```

## Running the game

Launching the binary without CLI flags opens the main menu (slideshow background, looping
`assets/music/main_menu_theme.wav`):

```powershell
.\build\x64-release\Release\AgeofAffinities.exe
```

| Menu entry | Behaviour |
|------------|-----------|
| Singleplayer | Fresh 2-player simulation with the second slot AI-controlled |
| Multiplayer | Player name, then **Host** (player count, map, biome Mixed/Grass/Snow/Sand, civil cap, fog; AI slots can be cycled off) or **Connect** (address, port) |
| Settings | Same Game/Audio/HUD panel as the in-game menu (fullscreen, Master/Music/SFX, Default vs AoE Style HUD). AoE Style hides the Default HUD center double-diamond over the command bar |
| Exit | Quits; `Esc` on the main menu does the same |

Main menu footer shows `GAME_VERSION` (`alpha_v0.2.1`). Lobby and lockstep joins require an **exact**
version match; mismatch → reject reason `Version mismatch` (`LOBBY_VERSION_MISMATCH_MESSAGE`).
Do not bump `GAME_VERSION` without an owner request — mismatched clients cannot share a lobby.

In the multiplayer lobby the host starts the match once every connected player is ready. Slots are
assigned automatically (host is player 1, peers fill the next free slots). **Resign** stays in the
match as a spectator-style resigned slot; **Exit to Main Menu** / leave ends the session (host
sends `HostEnded` so peers do not reconnect to a dead match). See [LOCKSTEP.md](LOCKSTEP.md).

**In-match UI notes**

| Feature | How |
|---------|-----|
| Map ping | Spyglass / pointer-mode button → click world or minimap; sim command `MapPing` (see [ECS.md](ECS.md)) |
| Diplomacy | HUD Diplomacy panel: Chat (All / Allies), Trades (`SendTrade`), Teams (`SetDiplomacy`) |
| Chat | Enter to compose; lockstep relays `NetMessageKind::Chat` (Allies filtered by ally mask) |

### Save / load (singleplayer)

Esc → game menu → **Save** / **Load**. Files live next to the exe under `saves/*.aoa`
(`sim::persistence::default_saves_directory()`).

| Topic | Behavior |
|-------|----------|
| Format | Snapshot magic `AOAS` / version **26** via `encode_sim_snapshot(sim, include_input_log=false)` — world + entities, **no** full input log |
| Manual save | Stem max 48 chars; rejects path punctuation (`/ \ : * ? " < > \|`); overwrite confirm if the file exists |
| Manual load | **Singleplayer only** — `GameMenuAction::Load` no-ops when a `LockstepSession` is attached |
| SP autosave | Every `AUTOSAVE_INTERVAL_MS` (5 min) → `saves/autosave.aoa` while the match is running |
| MP autosave | Host-only local write → `saves/autosave.mp.aoa` (not coordinated across peers; MP load is still open — [BACKLOG.md](BACKLOG.md)) |
| Code | `src/sim/persistence/save_game.*`; UI in `game_input.cpp` / `game_menu.hpp` |

**Pitfalls:** same snapshot v26 gap as reconnect — resign / match-outcome fields are not in the blob
(draft **#85**). Do not expect loading an MP autosave into a live lockstep session. Main menu has
no “Continue” entry; open a SP match and use **Load** from the in-game menu.

The `--lockstep-host` / `--lockstep-join` flags below bypass the menu and behave as before.

## Headless mode and harness

Same binary, no window — for desync/regression runs:

```powershell
.\build\x64-debug\Debug\AgeofAffinities.exe --headless --ticks 200
.\build\x64-debug\Debug\AgeofAffinities.exe --headless --ticks 200 --print-hash
.\build\x64-debug\Debug\AgeofAffinities.exe --headless --ticks 200 --expect-hash 0xcb4f9a25c25d549c
.\build\x64-debug\Debug\AgeofAffinities.exe --harness
.\build\x64-debug\Debug\AgeofAffinities.exe --net-smoke
```

| Mode | Use |
|------|-----|
| `--headless` | Run the default Earth spawn for N ticks (optional hash print/assert) |
| `--harness` | Run every `data/scenarios/*.json`, including command-replay scenarios |
| `--net-smoke` | In-process ENet host/client loopback; sends a `PlayerCommand` reliably |
| `--lockstep-smoke` | Two lockstep sessions in-process; host issues gather at tick 5; verifies hash match (`27001`) |
| `--lockstep-disconnect-smoke` | Client disconnect mid-match; host enters AI immediately and sim keeps advancing (`27200`) |
| `--lockstep-reconnect-smoke` | Three disconnect → AI → reconnect → live lockstep cycles; hash match each time (`27201`) |
| `--lockstep-4-smoke` | Four peers in-process; empty batches; hash match after 40 ticks (`27202`) |
| `--lockstep-4-disconnect-smoke` | Four peers; one client drops; per-slot AI; remaining peers stay in lockstep (`27203`) |
| `--lockstep-peer-silence-smoke` | Two peers; client stops advancing; host silence AI + disconnect (`27204`) |
| `--lockstep-4-reconnect-smoke` | Four peers; disconnect one slot, remaining advance, reconnect, hash match (`27207`) |
| `--snapshot-smoke` | Encode sim snapshot + input log, restore via replay, verify hash match |
| `--lockstep-host` | Lockstep host (player 1); graphical unless `--headless`; optional `--players N` or `--lockstep-players N` (2, 4, or 8) |
| `--lockstep-join HOST:PORT` | Lockstep client; graphical unless `--headless`; optional `--player-slot N` (2–8); same `--players` as host |

Full lockstep runbook (ports, reconnect, multi-peer caveats): [LOCKSTEP.md](LOCKSTEP.md).
Scale testing playbook (4–8 players, two-PC layouts): [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md)

For lockstep, `--ticks N` applies only with `--headless` (graphical sessions run until you close the window or the opponent disconnects). Headless lockstep defaults to 100 ticks without `--ticks`.

Default tick count for `--headless` (non-lockstep): `HEADLESS_DEFAULT_TICK_COUNT` in `src/core/constants.hpp` (100).

Full scenario format, roles, hash update steps, and pitfalls: [HARNESS.md](HARNESS.md).

## Assets

| Folder | Role |
|--------|------|
| `raw-assets/` | Gitignored source art (local only) |
| `assets/` + `data/` | Source trees packed into `assets.dat` next to the exe (POST_BUILD); loose copies are removed from the build output |
| `scenarios/` / `patterns/` | Copied next to the exe for lobby maps / Pattern Maker |

See [assets/README.md](../assets/README.md), [LOCKSTEP.md](LOCKSTEP.md) (runtime paths), and [DECISIONS.md](DECISIONS.md).

**Pack pitfall:** `aoa_pack_assets` walks every file under `assets/` and `data/`. Paths referenced from
`src/core/constants.hpp` (for example `SFX_LOOK_HERE_RELATIVE_PATH` →
`sfx/Cringemarine/look-here.wav`) must exist in the tree; a missing file aborts POST_BUILD and fails
CI before any smoke runs.

## Windows installer (`aoa-setup.exe`)

Unsigned NSIS setup (signing is deferred). Install [NSIS](https://nsis.sourceforge.io/) once:

```powershell
winget install NSIS.NSIS
```

Then:

```powershell
cmake --build build\x64-release --config Release --target aoa
cmake --build build\x64-release --config Release --target aoa_setup
```

Or: `.\scripts\package-setup.ps1`

Output: `build\x64-release\Release\aoa-setup.exe`. It installs `AgeofAffinities.exe`, SFML/runtime DLLs, `assets.dat`, `scenarios\`, and `patterns\`. Pattern Maker is an optional component. `aoa_pack_assets.exe` is not shipped.

## Open in Visual Studio

After `cmake --preset x64-debug`, open `build/x64-debug/age-of-affinities.sln` (generator name may vary). Pick **x64** and **Debug** or **Release** in the VS toolbar — do not use Win32.

## CI

GitHub Actions uses **Ninja + MSVC** presets (`ci-x64-debug`, `ci-x64-release`) because hosted runners do not expose the Visual Studio generator the same way as a local VS install. Local development keeps **Visual Studio 2022** presets (`x64-debug`, `x64-release`).

Workflow: `.github/workflows/build.yml` — runs on every push to `main` and on pull requests. After build it smokes `--headless --ticks 5`, `--net-smoke`, `--lockstep-smoke`, `--lockstep-disconnect-smoke`, `--lockstep-reconnect-smoke`, `--lockstep-4-smoke`, `--lockstep-4-disconnect-smoke`, `--lockstep-4-reconnect-smoke`, `--lockstep-peer-silence-smoke`, and `--snapshot-smoke` on both CI presets and runs `--harness` on `ci-x64-debug`. Keep scenario hashes green before merging sim changes. Note: the PowerShell smoke step does not fail the job on native non-zero exits — check logs when debugging flaky lockstep paths.

Lockstep uses a **2-tick input delay** (`LOCKSTEP_COMMAND_DELAY_TICKS`): commands are buffered, sent in `TickInputBatch`, then applied on both peers at the same execute tick. Do not enqueue locally before the batch is sent — that caused desync when moving units.

```powershell
# Terminal 1 — graphical (default)
.\build\x64-debug\Debug\AgeofAffinities.exe --lockstep-host --port 27000

# Terminal 2
.\build\x64-debug\Debug\AgeofAffinities.exe --lockstep-join 127.0.0.1:27000

# Headless scripted run (100 ticks default)
.\build\x64-debug\Debug\AgeofAffinities.exe --lockstep-host --headless --ticks 100
.\build\x64-debug\Debug\AgeofAffinities.exe --lockstep-join 127.0.0.1:27000 --headless --ticks 100
```
