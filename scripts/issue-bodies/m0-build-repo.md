## [BLOCKING] Build & repo setup

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M0.

- [ ] CMake project (don't fight VS2022 .sln files directly — CMake + VS2022 generator, or CMake + Claude Code, either way CMake is the source of truth)
- [ ] vcpkg or manual setup for: SFML, OpenGL loader (glad/glew), EnTT, ENet, a JSON lib (nlohmann/json)
- [x] Private GitHub repo, .gitignore, branch strategy (even solo: main + short-lived feature branches, so history stays bisectable when a desync bug shows up later)
- [ ] CI: a GitHub Action that just builds on push (catches "works on my machine" early, costs you nothing since GitHub Actions free tier covers a solo private repo easily)
