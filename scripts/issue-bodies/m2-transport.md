## [BLOCKING] Transport layer

Epic from [docs/BACKLOG.md](../../docs/BACKLOG.md) — M2.

- [x] ENet integration: connect, send/receive reliable messages — `EnetTransport`, dedicated network thread
- [x] Message serialization format for inputs (compact — player commands, not state) — `TickInputBatch`, `PlayerCommand` wire format in `src/net/`
