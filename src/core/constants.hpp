#pragma once

#include <cstdint>
#include <string_view>

namespace aoa::constants {

inline constexpr std::string_view WINDOW_TITLE = "Age of Affinities";
inline constexpr std::uint32_t DEFAULT_WINDOW_WIDTH = 1280U;
inline constexpr std::uint32_t DEFAULT_WINDOW_HEIGHT = 720U;

inline constexpr int SIM_TICKS_PER_SECOND = 20;
inline constexpr int TARGET_DISPLAY_FPS = 60;
inline constexpr int PLAYER_COMMAND_DELAY_TICKS = 1;
inline constexpr int OPENGL_MAJOR_VERSION = 3;
inline constexpr int OPENGL_MINOR_VERSION = 3;

inline constexpr int FIXED_FRACTION_BITS = 16;
inline constexpr std::int32_t FIXED_ONE_RAW = 1 << FIXED_FRACTION_BITS;

inline constexpr std::uint64_t HEADLESS_DEFAULT_TICK_COUNT = 100U;

inline constexpr int MAP_TEST_WIDTH = 64;
inline constexpr int MAP_TEST_HEIGHT = 64;

inline constexpr int MAP_MIN_SIZE = 32;
inline constexpr int MAP_MAX_SIZE = 512;

inline constexpr int RENDER_TILE_PIXELS = 16;

inline constexpr int RENDER_ISO_TILE_WIDTH = 32;
inline constexpr int RENDER_ISO_TILE_HEIGHT = 16;

inline constexpr float CAMERA_CLASSIC_MIN_ZOOM = 0.35F;
inline constexpr float CAMERA_CLASSIC_MAX_ZOOM = 2.5F;
inline constexpr float CAMERA_CLASSIC_ZOOM_STEP = 0.08F;
inline constexpr float CAMERA_CLASSIC_FRAME_PADDING = 0.92F;
inline constexpr int CAMERA_EDGE_SCROLL_MARGIN_PX = 24;
inline constexpr float CAMERA_EDGE_SCROLL_SPEED_PX_PER_SEC = 480.0F;
inline constexpr float CAMERA_KEYBOARD_PAN_SPEED_PX_PER_SEC = 420.0F;

inline constexpr float SELECTION_PICK_RADIUS_TILES = 0.36F;
inline constexpr int SELECTION_BOX_DRAG_THRESHOLD_PX = 6;

inline constexpr int HUD_PIXEL_SCALE = 2;
inline constexpr int HUD_CHAR_SPACING = 1;
inline constexpr int HUD_LINE_SPACING = 4;
inline constexpr float HUD_MARGIN_X = 12.0F;
inline constexpr float HUD_MARGIN_Y = 12.0F;
inline constexpr float HUD_TEXT_R = 0.95F;
inline constexpr float HUD_TEXT_G = 0.88F;
inline constexpr float HUD_TEXT_B = 0.55F;

inline constexpr float RENDER_FOREST_EXTRUDE = 0.14F;
inline constexpr float RENDER_UNIT_HEIGHT = 0.48F;
inline constexpr float RENDER_BUILDING_HEIGHT = 0.72F;
inline constexpr float RENDER_ENTITY_BASE_LIFT = 0.04F;
inline constexpr float RENDER_HEIGHT_SCREEN_SCALE = 18.0F;
inline constexpr float RENDER_DEPTH_GRID_SCALE = 0.0008F;
inline constexpr float RENDER_DEPTH_HEIGHT_SCALE = 0.02F;
inline constexpr float RENDER_SIDE_LIGHT_FACTOR = 0.62F;
inline constexpr float RENDER_AMBIENT_LIGHT = 0.88F;
inline constexpr float RENDER_SELECTION_OUTLINE_SCALE = 1.18F;
inline constexpr float RENDER_HOVER_OUTLINE_SCALE = 1.12F;
inline constexpr float RENDER_SELECTION_HIGHLIGHT_R = 0.45F;
inline constexpr float RENDER_SELECTION_HIGHLIGHT_G = 0.85F;
inline constexpr float RENDER_SELECTION_HIGHLIGHT_B = 0.35F;
inline constexpr float RENDER_HOVER_FRIENDLY_R = 0.95F;
inline constexpr float RENDER_HOVER_FRIENDLY_G = 0.82F;
inline constexpr float RENDER_HOVER_FRIENDLY_B = 0.18F;
inline constexpr float RENDER_HOVER_ENEMY_R = 0.90F;
inline constexpr float RENDER_HOVER_ENEMY_G = 0.35F;
inline constexpr float RENDER_HOVER_ENEMY_B = 0.35F;
inline constexpr float RENDER_HOVER_RESOURCE_R = 0.55F;
inline constexpr float RENDER_HOVER_RESOURCE_G = 0.70F;
inline constexpr float RENDER_HOVER_RESOURCE_B = 0.30F;

inline constexpr std::string_view EARTH_CIV_ID = "earth";
inline constexpr std::string_view WORKER_UNIT_ID = "worker";
inline constexpr std::string_view MILITIA_UNIT_ID = "militia";
inline constexpr std::string_view TOWN_CENTER_BUILDING_ID = "town_center";
inline constexpr std::string_view FOREST_PATCH_RESOURCE_ID = "forest_patch";

inline constexpr int PATHFIND_COST_SCALE = 10;
inline constexpr int PATHFIND_CARDINAL_STEP_COST = 10;
inline constexpr int PATHFIND_DIAGONAL_STEP_COST = 14;
inline constexpr int PATHFIND_KNIGHT_STEP_COST = 22;

inline constexpr char SPAWN_WORKER_HOTKEY = 'W';

} // namespace aoa::constants
