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

Output executable: `build/x64-debug/Debug/aoa.exe` or `build/x64-release/Release/aoa.exe`.

## Open in Visual Studio

After `cmake --preset x64-debug`, open `build/x64-debug/age-of-affinities.sln` (generator name may vary). Pick **x64** and **Debug** or **Release** in the VS toolbar — do not use Win32.

## CI

GitHub Actions uses **Ninja + MSVC** presets (`ci-x64-debug`, `ci-x64-release`) because hosted runners do not expose the Visual Studio generator the same way as a local VS install. Local development keeps **Visual Studio 2022** presets (`x64-debug`, `x64-release`).

Workflow: `.github/workflows/build.yml` — runs on every push to `main` and on pull requests.
