## Headless regression harness

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M1.

- [x] Scripted test scenarios (`data/scenarios/*.json`) — spawn default Earth setup, run N ticks, assert final state hash
- [x] CLI: `aoa --harness` runs all scenarios; `--headless --ticks N --expect-hash HEX` for ad-hoc checks
- [x] Baseline scenario: `earth_default` @ 200 ticks → `0x2783280bd432b0c8`
- [x] Command-replay scenario: `earth_player_commands` @ 150 ticks → `0x22aa666ab4523726`
