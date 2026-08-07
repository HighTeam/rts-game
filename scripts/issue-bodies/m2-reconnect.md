## Reconnect

Epic from [docs/BACKLOG.md](../../docs/BACKLOG.md) — M2.

- [x] On reconnect: client receives sim snapshot + input log; `ResyncReady` handshake; resumes live lockstep
- [x] Disconnect policy: **no global pause** — host sim continues immediately with AI for dropped player ([DECISIONS.md](../../docs/DECISIONS.md))
- [x] AI takeover on disconnect — `enter_ai_fallback()` on transport down (not after 30s freeze)
- [x] Reconnect grace — `LOCKSTEP_RECONNECT_GRACE_MS` (30s) for human return; AI runs during grace
- [x] Stale ENet peer fix — late DISCONNECT from old peer does not kill new connection
