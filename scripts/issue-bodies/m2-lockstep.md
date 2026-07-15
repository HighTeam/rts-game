## [BLOCKING] Lockstep sync

Epic from [docs/BACKLOG.md](../docs/BACKLOG.md) — M2. **Highest-risk milestone — protect time budget.**

- [ ] Turn/tick-based input collection: every client sends inputs for tick N; sim doesn't advance until all inputs for N are received
- [ ] Input delay buffer (send inputs for tick N+k, not tick N)
- [ ] Full input log from game start (every input, every tick) — reused by save/load and reconnect
- [ ] Live desync detection: exchange per-tick state hashes between clients
