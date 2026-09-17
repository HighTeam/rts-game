# HUD and in-match UI

Developer notes for the two shipped HUD layouts, layout hit-tests, and settings persistence.
Verified against `main` @ `alpha_v0.2.1` (`bfc6a29`).

## Intent

Players pick **Default** or **AoE Style** from Settings / Esc game menu. Layout math and
hit-tests must stay in sync with draw code — mismatched rects cause dead clicks or clicks that
fall through to the world.

Sim state (commands, diplomacy, map pings) is style-agnostic. Only screen chrome changes.

## Styles

| Enum | Label | Helper |
|------|-------|--------|
| `HudStyle::Default` (0) | `HUD: Default` | Grid diamond chrome (`HudGrid` 32×18) |
| `HudStyle::Aoe` (1) | `HUD: AoE Style` | Classic bottom panels (`hud_is_classic_aoe`) |

Constants / labels: `src/core/constants.hpp`. Cycle action: `GameMenuAction::CycleHudStyle`
→ `next_hud_style()` in `src/app/game_menu.hpp`.

Default is the factory value on `AppShellSettings` / `GameMenuState`.

## Module map

| Piece | Location | Role |
|-------|----------|------|
| Style enum + labels | `src/core/constants.hpp` | `HudStyle`, button strings |
| Grid units (Default) | `src/app/hud_grid.hpp` | 32×18 `HudGrid`; diamond point helpers |
| Panel frames + hit-tests | `src/app/command_panel.hpp` | `*_frame_rect`, `hit_test_*`, `hud_is_classic_aoe` |
| Draw | `src/render/hud_overlay.cpp` | Minimap, command/status panels, resource bar |
| Input | `src/app/game_input.cpp` | Menu / diplomacy / minimap / pointer-mode clicks |
| Persistence | `src/app/app_settings.cpp` | `settings.json` next to the exe |

## Layout differences

**Default**

- Side boxes and minimap use `HudGrid` unit coordinates (`HUD_DEFAULT_*` constants).
- Minimap chrome: outer + inner diamond strokes via `default_minimap_diamond_points` inside
  `draw_minimap_contents` (`hud_overlay.cpp`).
- Extra Default-only chrome: age title, unit portrait path in the status panel, minimap mode +
  pointer-mode (spyglass) buttons.

**AoE Style**

- Command panel = bottom-left strip; minimap / status = bottom-right strip (`bottom_panel_size`).
- **Skips** the Default minimap double-diamond stroke (`if (!hud_is_classic_aoe(...))` around
  `stroke_hud_polygon` — tip `bfc6a29`).
- Hit-tests use axis-aligned / diamond panel frames appropriate to AoE rects; Default-only
  buttons are not drawn and must not be required for core play.

When adding a new HUD control: update **frame helper**, **hit-test**, and **draw** for both
styles (or gate explicitly with `hud_is_classic_aoe`).

## Settings persistence

File: `settings.json` beside the executable (`SETTINGS_FILE_NAME`, version
`SETTINGS_FILE_VERSION` = 1). Load/save: `load_app_settings` / `save_app_settings`.

Relevant keys (among others):

| Key | Type | Notes |
|-----|------|-------|
| `hud_style` | int | `0` Default, `1` AoE; invalid values ignored |
| `show_perf_hud` | bool | FPS/TPS overlay — independent of `HudStyle` |
| `building_range_display` | int | Never / Selected / Always |
| `fullscreen`, `vsync`, `fps_limit`, volumes, `scroll_speed`, `player_name` | … | Shell-wide |

Menu and in-match Settings panels write the same file. Changing HUD style mid-match updates
`GameInput` / shell settings and persists on save paths in `application.cpp` / `app_shell.cpp`.

## Related in-match UI (style-agnostic)

| Feature | Entry | Notes |
|---------|-------|-------|
| Map ping | Pointer-mode / spyglass → click | `MapPing` command — [ECS.md](ECS.md) |
| Diplomacy | HUD Diplomacy button | Chat / Trades / Teams — [ECS.md](ECS.md) |
| Resign / Leave / Pause | Esc game menu | Wire behavior — [LOCKSTEP.md](LOCKSTEP.md) |
| SP Save / Load | Esc game menu | [BUILD.md](BUILD.md) |

## Constraints / pitfalls

- **Draw ↔ hit-test drift** — Prefer shared `*_frame_rect` helpers; do not hard-code pixel boxes
  in only one of draw or input.
- **AoE vs Default gating** — Code that assumes Default diamonds or pointer-mode buttons must
  check `hud_is_classic_aoe` (or the inverse) so AoE Style does not depend on hidden controls.
- **Perf HUD ≠ HudStyle** — `show_perf_hud` is a separate boolean overlay.
- **No sim desync** — Style never enters the state hash or snapshot; it is local chrome only.

## Related

- [BUILD.md](BUILD.md) — Settings menu entry, save/load
- [ECS.md](ECS.md) — Map pings, diplomacy commands, tick pipeline
- [LOCKSTEP.md](LOCKSTEP.md) — Network HUD ping display, resign / HostEnded
- [BACKLOG.md](BACKLOG.md) — GUI redesign epic still open for a deeper HUD pass
