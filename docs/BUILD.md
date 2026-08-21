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
| Multiplayer | Player name, then **Host** (player count, map, civil cap, fog) or **Connect** (address, port) |
| Settings | Same Game/Audio panel as the in-game menu (fullscreen, Master/Music/SFX) |
| Exit | Quits; `Esc` on the main menu does the same |

In the multiplayer lobby the host starts the match once every connected player is ready. Slots are
assigned automatically (host is player 1, peers fill the next free slots). Exiting a match through
the in-game menu's **Exit to Main Menu** disconnects the session and returns to the menu.

The `--lockstep-host` / `--lockstep-join` flags below bypass the menu and behave as before.

## Headless mode and harness

Same binary, no window — for desync/regression runs:

```powershell
.\build\x64-debug\Debug\AgeofAffinities.exe --headless --ticks 200
.\build\x64-debug\Debug\AgeofAffinities.exe --headless --ticks 200 --print-hash
.\build\x64-debug\Debug\AgeofAffinities.exe --headless --ticks 200 --expect-hash 0xc59dd1cc68525745
.\build\x64-debug\Debug\AgeofAffinities.exe --harness
.\build\x64-debug\Debug\AgeofAffinities.exe --net-smoke
```

| Mode | Use |
|------|-----|
| `--headless` | Run the default Earth spawn for N ticks (optional hash print/assert) |
| `--harness` | Run every `data/scenarios/*.json`, including command-replay scenarios |
| `--net-smoke` | In-process ENet host/client loopback; sends a `PlayerCommand` reliably |
| `--lockstep-smoke` | Two lockstep sessions in-process; host issues gather at tick 5; verifies hash match |
| `--lockstep-disconnect-smoke` | Client disconnect mid-match; host enters AI immediately and sim keeps advancing |
| `--lockstep-reconnect-smoke` | Three disconnect → AI → reconnect → live lockstep cycles in-process; verifies hash match each time |
| `--lockstep-4-smoke` | Four peers (host + 3 clients) in-process; empty batches; hash match after 40 ticks |
| `--snapshot-smoke` | Encode sim snapshot + input log, restore via replay, verify hash match |
| `--lockstep-host` | Lockstep host (player 1); graphical unless `--headless`; optional `--players N` or `--lockstep-players N` (2, 4, or 8) |
| `--lockstep-join HOST:PORT` | Lockstep client; graphical unless `--headless`; optional `--player-slot N` (2–8); same `--players` as host |

Scale testing playbook (4–8 players, two-PC layouts): [M3_SCALE_TESTING.md](M3_SCALE_TESTING.md)

For lockstep, `--ticks N` applies only with `--headless` (graphical sessions run until you close the window or the opponent disconnects). Headless lockstep defaults to 100 ticks without `--ticks`.

Default tick count for `--headless` (non-lockstep): `HEADLESS_DEFAULT_TICK_COUNT` in `src/core/constants.hpp` (100).

Full scenario format, roles, hash update steps, and pitfalls: [HARNESS.md](HARNESS.md).

## Assets

| Folder | Role |
|--------|------|
| `raw-assets/` | Gitignored source art (local only) |
| `assets/` | Shipped runtime copies (POST_BUILD copy next to `AgeofAffinities.exe`) |

See [assets/README.md](../assets/README.md) and [DECISIONS.md](DECISIONS.md).

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

Workflow: `.github/workflows/build.yml` — runs on every push to `main` and on pull requests. After build it smokes `--headless --ticks 5`, `--net-smoke`, `--lockstep-smoke`, `--lockstep-disconnect-smoke`, and `--lockstep-reconnect-smoke` on both CI presets and runs `--harness` on `ci-x64-debug`. Keep scenario hashes green before merging sim changes.

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
