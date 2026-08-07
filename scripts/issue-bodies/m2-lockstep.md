## [BLOCKING] Lockstep sync

Epic from [docs/BACKLOG.md](../../docs/BACKLOG.md) — M2. **Highest-risk milestone — protect time budget.**

- [x] Turn/tick-based input collection: every client sends inputs for tick N; sim doesn't advance until all inputs for N are received
- [x] Input delay buffer — `LOCKSTEP_COMMAND_DELAY_TICKS` (4 ticks @ 20 tps) via `submit_local_command()`
- [x] Full input log from game start — `CommandQueue::input_log()` + network `TickInputBatch`
- [x] Live desync detection: exchange per-tick state hashes between clients
- [x] Wire lockstep into graphical play — `--lockstep-host` / `--lockstep-join`
- [x] Dedicated network thread (ENet) — UI thread does not block on window drag
- [x] Render smoothness: tick-thread interpolation timing + try-lock draw (no long mutex hold during GL)
- [ ] 4/8-player input gating — M3 multi-peer transport (see `m2-tests.md` brutal 8-player LAN)
