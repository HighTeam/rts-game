#pragma once

#include <cstdint>
#include <string_view>

#include <array>

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

inline constexpr int SCENARIO_PLAYER1_TC_X = 8;
inline constexpr int SCENARIO_PLAYER1_TC_Y = 8;
inline constexpr int SCENARIO_PLAYER1_WORKER_X = 9;
inline constexpr int SCENARIO_PLAYER1_WORKER_Y = 8;
inline constexpr int SCENARIO_PLAYER1_MILITIA_X = 10;
inline constexpr int SCENARIO_PLAYER1_MILITIA_Y = 8;

inline constexpr int SCENARIO_PLAYER2_TC_X = 52;
inline constexpr int SCENARIO_PLAYER2_TC_Y = 52;
inline constexpr int SCENARIO_PLAYER2_WORKER_X = 51;
inline constexpr int SCENARIO_PLAYER2_WORKER_Y = 52;
inline constexpr int SCENARIO_PLAYER2_MILITIA_X = 50;
inline constexpr int SCENARIO_PLAYER2_MILITIA_Y = 52;

inline constexpr int SCENARIO_PLAYER1_FOREST_MIN_X = 20;
inline constexpr int SCENARIO_PLAYER1_FOREST_MAX_X = 28;
inline constexpr int SCENARIO_PLAYER1_FOREST_MIN_Y = 12;
inline constexpr int SCENARIO_PLAYER1_FOREST_MAX_Y = 18;

inline constexpr int SCENARIO_PLAYER2_FOREST_MIN_X = 36;
inline constexpr int SCENARIO_PLAYER2_FOREST_MAX_X = 44;
inline constexpr int SCENARIO_PLAYER2_FOREST_MIN_Y = 46;
inline constexpr int SCENARIO_PLAYER2_FOREST_MAX_Y = 52;

inline constexpr int MAP_MIN_SIZE = 32;
inline constexpr int MAP_MAX_SIZE = 512;
inline constexpr int MAX_PLAYER_SLOTS = 8;
inline constexpr int DEFAULT_UNIT_VISION_RANGE = 4;
inline constexpr int DEFAULT_WORKER_VISION_RANGE = 3;
inline constexpr int DEFAULT_STRUCTURE_VISION_RANGE = 6;
inline constexpr int DEFAULT_TOWN_CENTER_VISION_RANGE = 8;
inline constexpr float FOG_UNEXPLORED_BRIGHTNESS = 0.0F;
inline constexpr float FOG_EXPLORED_SHROUD_BRIGHTNESS = 0.42F;
inline constexpr float FOG_VISION_RADIUS_TILE_PADDING = 0.35F;

inline constexpr int RENDER_TILE_PIXELS = 16;

inline constexpr int RENDER_ISO_TILE_WIDTH = 32;
inline constexpr int RENDER_ISO_TILE_HEIGHT = 16;

inline constexpr float CAMERA_CLASSIC_MIN_ZOOM = 2.5F;
inline constexpr float CAMERA_CLASSIC_MAX_ZOOM = 7.0F;
inline constexpr float CAMERA_CLASSIC_START_ZOOM = 4.5F;
inline constexpr float CAMERA_CLASSIC_ZOOM_SMOOTH_RATE = 16.0F;
inline constexpr std::array<float, 10> CAMERA_CLASSIC_ZOOM_LEVELS = {
    2.5F,
    3.0F,
    3.5F,
    4.0F,
    4.5F,
    5.0F,
    5.5F,
    6.0F,
    6.5F,
    7.0F,
};
inline constexpr int CAMERA_CLASSIC_START_ZOOM_LEVEL_INDEX = 4;
inline constexpr float CAMERA_CLASSIC_FRAME_PADDING = 0.92F;
inline constexpr int CAMERA_EDGE_SCROLL_MARGIN_PX = 24;
inline constexpr float CAMERA_EDGE_SCROLL_SPEED_PX_PER_SEC = 480.0F;
inline constexpr float CAMERA_KEYBOARD_PAN_SPEED_PX_PER_SEC = 420.0F;

inline constexpr float SELECTION_PICK_RADIUS_TILES = 0.36F;
inline constexpr int SELECTION_BOX_DRAG_THRESHOLD_PX = 6;

inline constexpr int HUD_PIXEL_SCALE = 2;
inline constexpr int LOCKSTEP_WAITING_TITLE_PIXEL_SCALE = 4;
inline constexpr int HUD_CHAR_SPACING = 1;
inline constexpr int HUD_LINE_SPACING = 4;
inline constexpr float HUD_MARGIN_X = 12.0F;
inline constexpr float HUD_MARGIN_Y = 12.0F;
inline constexpr float HUD_TEXT_R = 0.95F;
inline constexpr float HUD_TEXT_G = 0.88F;
inline constexpr float HUD_TEXT_B = 0.55F;

inline constexpr float RENDER_FOREST_EXTRUDE = 0.14F;
inline constexpr float RENDER_UNIT_HEIGHT = 0.30F;
inline constexpr float RENDER_UNIT_RADIUS = 0.20F;
inline constexpr int RENDER_CYLINDER_SEGMENTS = 16;
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

// Units visually touch when center distance is 2 * radius; slide into this gap before melee hits land.
inline constexpr float MELEE_UNIT_COLLISION_RADIUS_TILES = 0.20F;
inline constexpr float MELEE_CONTACT_CENTER_DISTANCE_TILES =
    MELEE_UNIT_COLLISION_RADIUS_TILES * 2.0F + 0.02F;
inline constexpr float MELEE_CONTACT_CENTER_DISTANCE_SQ =
    MELEE_CONTACT_CENTER_DISTANCE_TILES * MELEE_CONTACT_CENTER_DISTANCE_TILES;
// Slightly wider than contact so Fixed rounding and symmetric slide still register a strike.
inline constexpr float MELEE_STRIKE_MAX_CENTER_DISTANCE_TILES = 0.55F;
inline constexpr float MELEE_STRIKE_MAX_CENTER_DISTANCE_SQ =
    MELEE_STRIKE_MAX_CENTER_DISTANCE_TILES * MELEE_STRIKE_MAX_CENTER_DISTANCE_TILES;
inline constexpr float MELEE_CONTACT_SLIDE_PER_TICK = 0.12F;

inline constexpr float RENDER_GRID_LINE_R = 0.12F;
inline constexpr float RENDER_GRID_LINE_G = 0.17F;
inline constexpr float RENDER_GRID_LINE_B = 0.10F;
inline constexpr float RENDER_GRID_LINE_WIDTH = 0.035F;
inline constexpr float RENDER_GRID_LINE_LIFT = 0.03F;

inline constexpr std::string_view EARTH_CIV_ID = "earth";
inline constexpr std::string_view WORKER_UNIT_ID = "worker";
inline constexpr std::string_view MILITIA_UNIT_ID = "militia";
inline constexpr std::string_view TOWN_CENTER_BUILDING_ID = "town_center";
inline constexpr std::string_view FOREST_PATCH_RESOURCE_ID = "forest_patch";

inline constexpr int PATHFIND_COST_SCALE = 10;
inline constexpr int PATHFIND_CARDINAL_STEP_COST = 10;
inline constexpr int PATHFIND_DIAGONAL_STEP_COST = 14;
inline constexpr int PATHFIND_KNIGHT_STEP_COST = 22;
inline constexpr int MOVE_REPATH_SEGMENT_LOOKAHEAD_TICKS = 1;
inline constexpr float MOVE_PATH_STEP_REACHED_TILE_DISTANCE = 0.08F;
inline constexpr float MOVE_GOAL_TILE_EDGE_INSET = 0.08F;
inline constexpr int ATTACK_PATH_DIRECT_LINE_MAX_STEPS = 512;

inline constexpr char SPAWN_WORKER_HOTKEY = 'W';

} // namespace aoa::constants
