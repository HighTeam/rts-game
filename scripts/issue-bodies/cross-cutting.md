## Cross-cutting early decisions

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md). Decide once — expensive to change later.

- [ ] Fixed-point math (M0) — expensive to retrofit
- [ ] Data-driven civ/unit/building definitions (M1) — expensive to retrofit
- [ ] Host migration policy (M3) — affects network code shape
- [ ] Disconnect/pause policy (M2) — affects UI and netcode together
- [ ] Tick rate — pick once; changing later touches balance, netcode timing, and input feel
