# Age of Affinities — release notes

Lobby and lockstep clients must share the same `GAME_VERSION`. Older builds cannot join a newer host.

## alpha_v0.2.1

Lockstep reconnect that survives a second drop, resign/host-leave, and playability polish.

- Mid-match reconnect: host and peer apply the same snapshot (pending orders, map pings, hash caches); handshake no longer freezes or DESYNCs on the second reconnect
- Resign stays in the match and announces in chat; host leave ends the game for everyone (no frozen host window, no reconnect to a dead match)
- Lobby: AI slots can be cycled off; biome presets (Mixed / Grass / Snow / Sand)
- Mill / berry depth sort; rocks on the map; first Town Center waives gold and mana
- Map ping (spyglass) on the world and minimap
- Training that finishes at 100% but cannot spawn (pop cap or no free tile) refunds the cost and unlocks Town Center / Barracks / Mage Academy
- Ctrl+A, Ctrl+C, and Ctrl+V on menu fields, save/load filename, and in-game / diplomacy chat
- Stronger disconnected-player AI; worker stand retry; longer pathfinding steps

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

## alpha_v0.1

Playable Earth economy and combat buildings, plus lockstep reconnect hardening on `main`.

- Mill, Mining camp, Barracks, Mage academy, Tower, Market
- Team-color banner masks; House visual variants
- Mid-match lobby rejoin and claim tokens
- Multi-peer reconnect: snapshot targeting, batch gates, hash broadcast, stale render snapshot after restore
- HUD polish on the classic command panel

## alpha_v0.0.3

Fuller Earth loop.

- `--lockstep-players` / `--player-slot`; headless join against a graphical host
- LAN soak scripts
- House, Lumber camp, Extractor, Mana lake; civil cap and mana rules
- Save/load and autosave (single-player)

## alpha_v0.0.2

Stability on that 2/4-player lockstep.

- `--net-smoke` and reconnect smoke
- AI takeover when a peer drops
- Fix for desync when issuing move commands (input-batch race)

## alpha_v0.0.1

First ever playable build, including multiplayer (exactly 2 or 4 players on lockstep LAN).

- CMake/vcpkg Windows build, fixed-point sim, EnTT ECS, OpenGL/SFML window
- Earth civ JSON archetypes: worker, militia, Town Center
- Gather/deposit, combat, 16-way pathfinding, deterministic state hash
- Classic isometric camera, map tiles, HUD overlay for wood and carry
- ENet lockstep: input batches, per-tick hash exchange, graphical play
- Main menu, multiplayer lobby, packed `assets.dat`
- Regression harness and command-replay tooling
