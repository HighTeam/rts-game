## 8-player scaling

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M3.

- [x] Multi-peer ENet host (replace 1:1 `MAX_PEERS = 2` model) — host accepts up to 7 clients
- [x] N-way input batch gating per tick (bitmask per execute tick)
- [ ] Headless multi-join CLI (`--player-slot`) + soak scripts for 4/8 on **one PC**
- [x] CI: `--lockstep-4-smoke` (in-process localhost)
- [ ] CI: `--lockstep-8-smoke`, `--lockstep-8-disconnect-smoke`
- [ ] **2-PC LAN layout** — host + headless peers split across machines (see [docs/M3_SCALE_TESTING.md](../../docs/M3_SCALE_TESTING.md); no 4–8 physical PCs required)
- [ ] Bandwidth/perf notes from 8-headless localhost soak
- [ ] **Brutal 8-player acceptance** — automated smokes + 2-PC split soak; see [m2-tests.md](m2-tests.md) § “8-player brutal test”
- [ ] Host migration policy decision: if host disconnects, match ends or another client takes over?
