## Polish & Debt

Epic from [docs/BACKLOG.md](../../docs/BACKLOG.md). Stabilize the current Earth loop before map/age pivots.

### Open netcode / persistence

- [ ] Multiplayer save/load — coordinated multi-peer load (encode already has snapshot + input log)
- [ ] LAN brutal soak — 8 players (deferred unless needed)

### Open gameplay / engine debt

- [ ] Combat attack pathfinding — dog-leg / double-diagonal; see [pathfinding-combat.md](pathfinding-combat.md)
- [ ] Unit collision — harden unit–unit blocking; see [unit-collision.md](unit-collision.md)
- [ ] Extractor / Mana lake sprite offsets — visual pass
- [ ] Work interact / stand range tuning (`WORK_INTERACT_RANGE_TILES`)
- [ ] Hitbox / movement — no walking unit center through 1×1 resources or buildings mid-segment
