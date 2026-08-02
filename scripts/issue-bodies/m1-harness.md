## Headless regression harness

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M1.

- [x] Scripted test scenarios (`data/scenarios/*.json`) — spawn default Earth setup, run N ticks, assert final state hash
- [x] CLI: `aoa --harness` runs all scenarios; `--headless --ticks N --expect-hash HEX` for ad-hoc checks
- [x] Baseline scenario: `earth_default` @ 200 ticks → `0x2fc60e4739468cfc`
