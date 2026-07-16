## [BLOCKING] Build & repo setup

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M0.

- [x] CMake project (don't fight VS2022 .sln files directly — CMake + VS2022 generator, or CMake + Claude Code, either way CMake is the source of truth)
- [x] vcpkg or manual setup for: SFML, OpenGL loader (glad/glew), EnTT, ENet, a JSON lib (nlohmann/json)
- [x] Private GitHub repo, .gitignore, branch strategy (even solo: main + short-lived feature branches, so history stays bisectable when a desync bug shows up later)
- [x] HighTeam organization; `rts-game` (private) + `rts-game-web` (public) under org
- [x] CI: a GitHub Action that just builds on push (catches "works on my machine" early, costs you nothing since GitHub Actions free tier covers a solo private repo easily)
- [x] x64 Debug and Release presets only (no x86 — Windows 10/11 64-bit target)
