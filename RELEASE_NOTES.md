# Age of Affinities — release notes

Lobby and lockstep clients must share the same `GAME_VERSION`. Older builds cannot join a newer host.

## alpha_v0.0.1

First playable single-player slice (M0–M1).

- CMake/vcpkg Windows build, fixed-point sim, EnTT ECS, OpenGL/SFML window
- Earth civ JSON archetypes: worker, militia, Town Center
- Gather/deposit, combat, 16-way pathfinding, deterministic state hash
- Classic isometric camera, map tiles, HUD overlay for wood and carry
- Regression harness and command-replay tooling

## alpha_v0.0.2

Two-player lockstep (M2).

- ENet transport and `--net-smoke`
- Lockstep input batches and per-tick hash exchange
- Graphical lockstep play (not headless-only)
- Reconnect smoke path and AI takeover when a peer drops
- Fix for desync when issuing move commands (input-batch race)

## alpha_v0.0.3

Multi-peer LAN, menu, and first shippable Earth loop (through tagged `alpha_v0.0.4` on `9fc05f2`).

- 4-player lockstep transport, `--lockstep-players` / `--player-slot`
- Headless join against a graphical host; LAN soak scripts
- Main menu, multiplayer lobby, packed `assets.dat`
- House, Lumber camp, Extractor, Mana lake; civil cap and mana rules
- Save/load and autosave (single-player)

## alpha_v0.1

Playable Earth economy and combat buildings, plus lockstep reconnect hardening on `main`.

- Mill, Mining camp, Barracks, Mage academy, Tower, Market
- Team-color banner masks; House visual variants
- Mid-match lobby rejoin and claim tokens
- Multi-peer reconnect: snapshot targeting, batch gates, hash broadcast, stale render snapshot after restore
- HUD polish on the classic command panel

## alpha_v0.2

Default HUD, diplomacy, more Earth buildings, and reconnect that stays in lockstep after restore.

- Default diamond HUD: resource bar, civ logos, unit portraits (male/female, spawn-stable), command/info panels
- Diplomacy: chat (all/allies), trades, teams / ally victory
- Farm, Garden, Reservoir; Tower and unit combat tuning
- Voluntary leave/resign (slot not reclaimable; no false AI takeover)
- Snapshot restore no longer inflates `player_stats` via `spawn_*` (immediate post-reconnect desync)
- Minimap camera diamond drawn above the map texture when a unit is selected
- Pathfinding and fog/visibility fixes
- Windows exe name `AgeofAffinities`; unsigned NSIS setup target (`aoa_setup`)
- CI: `--lockstep-4-reconnect-smoke`
