## RMG & Fairness

Epic from [docs/BACKLOG.md](../../docs/BACKLOG.md). Start after Map Redesign vocabulary exists.

### Pattern / script pipeline

- [x] Map pattern format (current `.pattern` + Pattern Maker)
- [x] Generator: bases, resources, roads (Crossing hardcoded; Commons + Other… files)
- [x] Deterministic seed; lockstep-safe (same seed ⇒ same map)

### Accepted current gen (polish later)

- [ ] Commons authored at 96; unlocked for 48/64/96/128 but offsets do not scale
- [ ] Per-size fairness pass
- [ ] Crossing is 64×64 / 2p locked

### Fairness & validation

- [ ] Resource / start balance checks
- [ ] Reject or repair unfair rolls
- [ ] Dev tools: dump seed, preview, regenerate
