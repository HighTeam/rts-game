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
.\build\x64-debug\Debug\aoa.exe --headless --ticks 200 --expect-hash 0xc59dd1cc68525745
.\build\x64-debug\Debug\aoa.exe --harness
.\build\x64-debug\Debug\aoa.exe --net-smoke
```

| Mode | Use |
|------|-----|
| `--headless` | Run the default Earth spawn for N ticks (optional hash print/assert) |
| `--harness` | Run every `data/scenarios/*.json`, including command-replay scenarios |
| `--net-smoke` | In-process ENet host/client loopback; sends a `PlayerCommand` reliably |

Default tick count without `--ticks`: `HEADLESS_DEFAULT_TICK_COUNT` in `src/core/constants.hpp` (100).

Full scenario format, roles, hash update steps, and pitfalls: [HARNESS.md](HARNESS.md).

## Assets

| Folder | Role |
|--------|------|
| `raw-assets/` | Gitignored source art (local only) |
| `assets/` | Shipped runtime copies (POST_BUILD copy next to `aoa.exe`) |

See [assets/README.md](../assets/README.md) and [DECISIONS.md](DECISIONS.md).

## Open in Visual Studio

After `cmake --preset x64-debug`, open `build/x64-debug/age-of-affinities.sln` (generator name may vary). Pick **x64** and **Debug** or **Release** in the VS toolbar — do not use Win32.

## CI

GitHub Actions uses **Ninja + MSVC** presets (`ci-x64-debug`, `ci-x64-release`) because hosted runners do not expose the Visual Studio generator the same way as a local VS install. Local development keeps **Visual Studio 2022** presets (`x64-debug`, `x64-release`).

Workflow: `.github/workflows/build.yml` — runs on every push to `main` and on pull requests. After build it smokes `--headless --ticks 5` and `--net-smoke` on both CI presets and runs `--harness` on `ci-x64-debug`. Keep scenario hashes green before merging sim changes.
