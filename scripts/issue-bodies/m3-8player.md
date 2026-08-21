## 8-player scaling

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M3.

- [x] Multi-peer ENet host (replace 1:1 `MAX_PEERS = 2` model) — host accepts up to 7 clients
- [x] N-way input batch gating per tick (bitmask per execute tick)
- [x] 4-player real LAN (RadminVPN) acceptance — omit 8-player tests for current M3 pass
- [ ] Headless multi-join CLI (`--player-slot`) + soak scripts for 4/8 on **one PC** (deferred with 8p)
- [x] CI: `--lockstep-4-smoke` (in-process localhost)
- [ ] CI: `--lockstep-8-smoke`, `--lockstep-8-disconnect-smoke` (deferred)
- [ ] **2-PC LAN layout** for 8p — deferred (see [docs/M3_SCALE_TESTING.md](../../docs/M3_SCALE_TESTING.md))
- [ ] Bandwidth/perf notes from 8-headless localhost soak (deferred)
- [ ] **Brutal 8-player acceptance** — deferred
- [x] Host migration policy decision: **no host migration** — clients exit if host is gone ([docs/DECISIONS.md](../../docs/DECISIONS.md))
