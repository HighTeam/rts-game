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

## Headless mode and harness

Same binary, no window — for desync/regression runs:

```powershell
.\build\x64-debug\Debug\aoa.exe --headless --ticks 200
.\build\x64-debug\Debug\aoa.exe --headless --ticks 200 --print-hash
.\build\x64-debug\Debug\aoa.exe --headless --ticks 200 --expect-hash 0x7982a0643f3bcb76
.\build\x64-debug\Debug\aoa.exe --harness
.\build\x64-debug\Debug\aoa.exe --net-smoke
```

The `--expect-hash` example matches `data/scenarios/earth_default.json` on this tree. Refresh it after intentional sim changes.

| Mode | Use |
|------|-----|
| `--headless` | Run the default Earth spawn for N ticks (optional hash print/assert) |
| `--harness` | Run every `data/scenarios/*.json`, including command-replay scenarios |
| `--net-smoke` | In-process ENet host/client loopback; sends a `PlayerCommand` reliably |
| `--lockstep-smoke` | Two lockstep sessions in-process; host issues gather at tick 5; verifies hash match |
| `--lockstep-disconnect-smoke` | Client disconnect mid-match; host enters AI immediately and sim keeps advancing |
| `--lockstep-reconnect-smoke` | Three disconnect → AI → reconnect → live lockstep cycles in-process; verifies hash match each time |
| `--lockstep-4-smoke` | Four peers (host + 3 clients) in-process; empty batches; local hash match after 40 ticks |
| `--snapshot-smoke` | Encode sim snapshot + input log, restore, verify hash match (in CI) |
| `--snapshot-double-spawn-smoke` | Snapshot roundtrip, then two wire `SpawnWorker`s stay in sync (local) |
| `--snapshot-reconnect-smoke` | Long AI/spawn run, then encode/restore (local) |
| `--snapshot-heavy-smoke` | Busy two-slot workload, then encode/restore (local) |
| `--lockstep-host` | Lockstep host (player 1); graphical unless `--headless` |
| `--lockstep-join HOST:PORT` | Lockstep client (player 2); graphical unless `--headless` |
| `--lockstep-debug` | Write `logs/lockstep_*.log` next to the binary (host/join and LAN soaks) |

For lockstep, `--ticks N` applies only with `--headless` (graphical sessions run until you close the window or the opponent disconnects). Headless lockstep defaults to 100 ticks without `--ticks`.

Default tick count for `--headless` (non-lockstep): `HEADLESS_DEFAULT_TICK_COUNT` in `src/core/constants.hpp` (100).

`main.cpp` treats these as exclusive modes. First match wins: harness → net/lockstep/snapshot smokes → lockstep host/join → headless → graphical. Unknown flags throw (`parse_launch_options`); there is no `--player-slot` yet. `--help` / `-h` prints a short usage string that omits the local `--snapshot-*-smoke` stress flags and `--lockstep-debug` — prefer this table and [LOCKSTEP.md](LOCKSTEP.md) as the full CLI catalog.

Full scenario format, roles, hash update steps, and pitfalls: [HARNESS.md](HARNESS.md). Lockstep/reconnect/snapshot details: [LOCKSTEP.md](LOCKSTEP.md).

## Runtime data paths

There is **no** `AOA_DATA_DIR` environment variable. Paths resolve in `src/core/runtime_paths.cpp`:

1. Compile-time `AOA_RUNTIME_ROOT` — set in `CMakeLists.txt` to `${CMAKE_SOURCE_DIR}` so a binary built from this tree can open the source `data/` and `assets/` even when cwd is elsewhere.
2. POST_BUILD — `cmake` copies `${CMAKE_SOURCE_DIR}/data` and `.../assets` next to `aoa.exe` (`$<TARGET_FILE_DIR:aoa>/…`).
3. Search order for `default_data_directory()`: `<exe>/data` (must contain `archetypes/`), then `AOA_RUNTIME_ROOT/data`, then cwd `data/`. Logs go under `<exe>/logs` when the exe path is known (`default_logs_directory()`).

If harness/smokes fail with missing scenarios or archetypes, rebuild (to refresh the POST_BUILD copy) or run a binary whose `AOA_RUNTIME_ROOT` still points at this checkout.

## Assets

| Folder | Role |
|--------|------|
| `raw-assets/` | Gitignored source art (local only) |
| `assets/` | Shipped runtime copies (POST_BUILD copy next to `aoa.exe`) |
| `data/` | Archetypes, civ JSON, harness scenarios (also POST_BUILD-copied next to the binary) |

See [assets/README.md](../assets/README.md) and [DECISIONS.md](DECISIONS.md).

## Open in Visual Studio

After `cmake --preset x64-debug`, open `build/x64-debug/age-of-affinities.sln` (generator name may vary). Pick **x64** and **Debug** or **Release** in the VS toolbar — do not use Win32.

## CI

GitHub Actions uses **Ninja + MSVC** presets (`ci-x64-debug`, `ci-x64-release`) because hosted runners do not expose the Visual Studio generator the same way as a local VS install. Local development keeps **Visual Studio 2022** presets (`x64-debug`, `x64-release`).

Workflow: `.github/workflows/build.yml` — runs on every push to `main` and on pull requests. After build it smokes `--headless --ticks 5`, `--net-smoke`, `--lockstep-smoke`, `--lockstep-disconnect-smoke`, `--lockstep-reconnect-smoke`, `--lockstep-4-smoke`, and `--snapshot-smoke` on both CI presets and runs `--harness` on `ci-x64-debug`. Keep scenario hashes green before merging sim changes.

Lockstep uses a **2-tick input delay** (`LOCKSTEP_COMMAND_DELAY_TICKS`): commands are buffered, sent in `TickInputBatch`, then applied on both peers at the same execute tick. Do not enqueue locally before the batch is sent — that caused desync when moving units. Architecture, reconnect, wire kinds, and pitfalls: [LOCKSTEP.md](LOCKSTEP.md).

```powershell
# Terminal 1 — graphical (default)
.\build\x64-debug\Debug\aoa.exe --lockstep-host --port 27000

# Terminal 2
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000

# Headless scripted run (100 ticks default)
.\build\x64-debug\Debug\aoa.exe --lockstep-host --headless --ticks 100
.\build\x64-debug\Debug\aoa.exe --lockstep-join 127.0.0.1:27000 --headless --ticks 100
```
