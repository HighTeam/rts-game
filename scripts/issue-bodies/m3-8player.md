## 8-player scaling

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M3.

- [ ] Multi-peer ENet host (replace 1:1 `MAX_PEERS = 2` model)
- [ ] N-way input batch gating per tick (all connected humans + AI for dropped slots)
- [ ] Bandwidth/perf test with 8 simulated clients (fake most as bots on one machine for dev)
- [ ] **Brutal 8-player LAN acceptance** — see [m2-tests.md](m2-tests.md) § “8-player brutal test” (M2 exit criterion once scaling lands)
- [ ] Host migration policy decision: if host disconnects, match ends or another client takes over?
