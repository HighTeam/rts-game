#pragma once

#include <cstdint>
#include <string_view>

#include <array>

namespace aoa::constants {

inline constexpr std::string_view WINDOW_TITLE = "Age of Affinities";
inline constexpr std::uint32_t DEFAULT_WINDOW_WIDTH = 1280U;
inline constexpr std::uint32_t DEFAULT_WINDOW_HEIGHT = 720U;

inline constexpr int SIM_TICKS_PER_SECOND = 20;
inline constexpr int SIM_MAX_TICKS_PER_FRAME = 2;
inline constexpr float SIM_MAX_FRAME_DELTA_SECONDS = 0.25F;
inline constexpr int TOWN_CENTER_FOOTPRINT_TILES = 3;
inline constexpr int HOUSE_FOOTPRINT_TILES = 2;
inline constexpr int WORKER_GATHER_INTERVAL_TICKS = 35;
inline constexpr int TARGET_DISPLAY_FPS = 60;
inline constexpr bool RENDER_VERTICAL_SYNC = true;
inline constexpr bool RENDER_MOUSE_CAPTURE_DEFAULT = true;
inline constexpr std::string_view VIDEO_MOUSE_CAPTURE_ENABLED_LABEL = "Mouse capturing: Enabled";
inline constexpr std::string_view VIDEO_MOUSE_CAPTURE_DISABLED_LABEL = "Mouse capturing: Disabled";
inline constexpr std::string_view CONSOLE_DEBUG_ARG = "--console-debug";
inline constexpr int VIDEO_FPS_30 = 30;
inline constexpr int VIDEO_FPS_60 = 60;
inline constexpr int VIDEO_FPS_120 = 120;
inline constexpr int VIDEO_FPS_UNLIMITED = 0;
inline constexpr std::array<int, 4> VIDEO_FPS_PRESETS{{
    VIDEO_FPS_30,
    VIDEO_FPS_60,
    VIDEO_FPS_120,
    VIDEO_FPS_UNLIMITED,
}};
inline constexpr int RENDER_GROUND_SCREEN_CULL_PAD_TILES = 1;
inline constexpr int RENDER_OBJECT_SCREEN_CULL_PAD_TILES = 6;
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
inline constexpr int JOB_RETARGET_VISION_HALF_DIVISOR = 2;
inline constexpr float FOG_UNEXPLORED_BRIGHTNESS = 0.0F;
inline constexpr float FOG_EXPLORED_SHROUD_BRIGHTNESS = 0.42F;
inline constexpr float RENDER_CLEAR_R = 0.05F;
inline constexpr float RENDER_CLEAR_G = 0.06F;
inline constexpr float RENDER_CLEAR_B = 0.08F;
// The desktop compositor blends the window using framebuffer alpha, so the default
// framebuffer must always end the frame fully opaque.
inline constexpr float RENDER_CLEAR_A = 1.0F;
inline constexpr float MENU_CLEAR_R = 0.02F;
inline constexpr float MENU_CLEAR_G = 0.03F;
inline constexpr float MENU_CLEAR_B = 0.04F;
inline constexpr float FOG_VISION_RADIUS_TILE_PADDING = 0.35F;
inline constexpr int FOG_BLUR_RADIUS_TILES = 1;
inline constexpr float FOG_BLUR_SIGMA = 0.65F;
inline constexpr float FOG_LIVE_EDGE_FADE_WIDTH_TILES = 0.5F;

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
inline constexpr float CAMERA_SCROLL_SPEED_MIN = 0.25F;
inline constexpr float CAMERA_SCROLL_SPEED_MAX = 2.0F;
inline constexpr float CAMERA_SCROLL_SPEED_DEFAULT = 1.0F;
inline constexpr int CAMERA_MAP_BOUNDS_PADDING_TILES = 4;
inline constexpr int RENDER_GROUND_CULL_PAD_TILES = 2;
inline constexpr int RENDER_OBJECT_CULL_PAD_TILES = 4;

inline constexpr int HUD_ICON_TILE_SIZE_PX = 32;
inline constexpr int HUD_ICON_DRAW_SIZE_PX = 48;
inline constexpr int HUD_ICON_OPTION_INSET_PX = 4;
inline constexpr int HUD_ICON_TEXT_GAP_PX = 4;
inline constexpr int HUD_OPTIONS_BUTTON_PRESS_OFFSET_PX = 2;
inline constexpr float HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN = 1.22F;
inline constexpr float HUD_OPTIONS_BUTTON_PRESS_DARKEN = 0.78F;
inline constexpr int MOVE_BLOCKED_REPATH_COOLDOWN_TICKS = 5;
/// Extra world-space samples along a move segment so corner-cuts cannot skip solid 1x1 tiles.
inline constexpr int MOVE_SEGMENT_SOLID_SAMPLE_COUNT = 8;
// Pause this many ticks on a radius block, then drop the segment and repath.
inline constexpr int MOVE_SEGMENT_RADIUS_BLOCK_WAIT_TICKS = 8;

// P1..P8: Red, Blue, Green, Yellow, Purple, Gray, Cyan, Pink
inline constexpr std::array<std::array<float, 3>, 8> PLAYER_SLOT_COLOR_RGB{{
    {{0.90F, 0.22F, 0.20F}},
    {{0.25F, 0.45F, 0.95F}},
    {{0.25F, 0.80F, 0.30F}},
    {{0.95F, 0.85F, 0.20F}},
    {{0.70F, 0.30F, 0.85F}},
    {{0.65F, 0.65F, 0.68F}},
    {{0.20F, 0.85F, 0.90F}},
    {{0.95F, 0.45F, 0.75F}},
}};
inline constexpr std::array<std::string_view, 8> PLAYER_COLOR_NAMES{{
    "Red",
    "Blue",
    "Green",
    "Yellow",
    "Purple",
    "Gray",
    "Cyan",
    "Pink",
}};
inline constexpr int MAP_PATTERN_BUILTIN_COUNT = 2;
inline constexpr int MAP_PATTERN_COUNT = 3;
inline constexpr std::uint8_t MAP_PATTERN_CROSSING_INDEX = 0U;
inline constexpr std::uint8_t MAP_PATTERN_COMMONS_INDEX = 1U;
inline constexpr std::uint8_t MAP_PATTERN_OTHER_INDEX = 2U;
inline constexpr std::array<std::string_view, MAP_PATTERN_COUNT> MAP_PATTERN_NAMES{{
    "Crossing",
    "Commons",
    "Other...",
}};
inline constexpr std::string_view PATTERNS_DIRECTORY_NAME = "patterns";
inline constexpr std::string_view PATTERN_FILE_EXTENSION = ".pattern";
inline constexpr std::string_view PATTERN_FILE_FILTER_PATTERN = "*.pattern";
inline constexpr std::string_view PATTERN_RAW_FILE_EXTENSION = ".raw.pattern";
inline constexpr std::string_view PATTERN_RAW_FILE_FILTER_PATTERN = "*.raw.pattern";
inline constexpr std::string_view PATTERN_RUNTIME_KIND = "aoa.pattern";
inline constexpr std::string_view PATTERN_EDITOR_KIND = "aoa.raw.pattern";
inline constexpr int PATTERN_FILE_VERSION = 1;
inline constexpr int PATTERN_MAX_PAYLOAD_BYTES = 262144;
inline constexpr int PATTERN_DEFAULT_MIN_PLAYERS = 2;
inline constexpr int PATTERN_DEFAULT_MAX_PLAYERS = 8;
inline constexpr int PATTERN_UNLIMITED_MIN_PLAYERS = 1;
inline constexpr int PATTERN_FIXED_PLAYERS_MIN = 2;
inline constexpr int PATTERN_FIXED_PLAYERS_MAX = 8;
inline constexpr int PATTERN_MIN_MAP_SIZE = 32;
inline constexpr int PATTERN_MAX_MAP_SIZE = 128;
inline constexpr std::array<int, 4> PATTERN_MAP_SIZE_PRESETS{{48, 64, 96, 128}};
inline constexpr int PATTERN_DEFAULT_MAP_SIZE_PRESET_INDEX = 1;
inline constexpr int PATTERN_MAP_SIZE_ANY_OPTION_INDEX =
    static_cast<int>(PATTERN_MAP_SIZE_PRESETS.size());
inline constexpr int PATTERN_MAP_SIZE_OPTION_COUNT = PATTERN_MAP_SIZE_ANY_OPTION_INDEX + 1;
inline constexpr int PATTERN_MAKER_SIZE_BUTTON_WIDTH = 72;
inline constexpr int PATTERN_MAKER_SIZE_BUTTON_GAP = 8;
inline constexpr int PATTERN_DEFAULT_MIN_START_SEPARATION = 12;
inline constexpr int PATTERN_START_PLAZA_RADIUS = 8;
inline constexpr int PATTERN_START_CLEAR_RADIUS = PATTERN_START_PLAZA_RADIUS;
inline constexpr int PATTERN_START_ECONOMY_RADIUS = 14;
inline constexpr int PATTERN_START_BERRY_MIN_RING = 8;
inline constexpr int PATTERN_START_BERRY_MAX_RING = 12;
inline constexpr int PATTERN_START_BERRY_COUNT = 6;
inline constexpr int PATTERN_START_STRAY_TREE_MIN_RING = 8;
inline constexpr int PATTERN_START_STRAY_TREE_MAX_RING = 12;
inline constexpr int PATTERN_START_STRAY_TREE_COUNT = 6;
inline constexpr int PATTERN_START_GOLD_MIN_RING = 10;
inline constexpr int PATTERN_START_GOLD_MAX_RING = 14;
inline constexpr int PATTERN_START_GOLD_COUNT = 4;
inline constexpr int PATTERN_START_LAKE_MIN_RING = 10;
inline constexpr int PATTERN_START_LAKE_MAX_RING = 14;
inline constexpr int PATTERN_START_WOODLINE_MIN_RING = 14;
inline constexpr int PATTERN_START_WOODLINE_MAX_RING = 18;
inline constexpr int PATTERN_START_WOODLINE_TILES = 70;
inline constexpr int PATTERN_START_RING_RADIUS_PERCENT = 38;
inline constexpr int PATTERN_START_RING_SNAP_SLACK = 2;
inline constexpr int PATTERN_BORDER_TREE_DEPTH = 3;
inline constexpr int PATTERN_RANDOM_STRAY_TREE_DEFAULT = 24;
inline constexpr int PATTERN_RANDOM_STRAY_TREE_MAX = 96;
inline constexpr int PATTERN_CIRCLE_SEGMENTS = 48;
inline constexpr float PATTERN_START_GOLD_ANGLE = 0.4F;
inline constexpr float PATTERN_PI = 3.14159265F;
inline constexpr float PATTERN_TAU = 6.28318531F;
inline constexpr float PATTERN_START_RING_BASE_ANGLE = 3.92699082F;
inline constexpr int PATTERN_RESOURCE_CLUSTER_RADIUS = 2;
inline constexpr int PATTERN_START_RESOURCE_SEPARATION = 6;
inline constexpr int PATTERN_RESOURCE_SNAP_RADIUS = 8;
inline constexpr int PATTERN_ROAD_HALF_WIDTH = 2;
inline constexpr int PATTERN_START_ACCESS_ROAD_HALF_WIDTH = 3;
inline constexpr int PATTERN_START_ACCESS_REACH =
    PATTERN_START_ECONOMY_RADIUS + PATTERN_RESOURCE_SNAP_RADIUS;
inline constexpr int PATTERN_FOREST_FILL_ROAD_HALF_WIDTH = 3;
inline constexpr int PATTERN_FOREST_FILL_COVERAGE_PERCENT = 50;
inline constexpr int PATTERN_FOREST_BLOB_BASE_TILES = 70;
inline constexpr int PATTERN_FOREST_DEFAULT_DENSITY = 35;
inline constexpr int PATTERN_FOREST_DEFAULT_PATCH_SIZE = 4;
inline constexpr int PATTERN_FOREST_DEFAULT_RANGE = 6;
inline constexpr int PATTERN_FOREST_MIN_RANGE = 2;
inline constexpr int PATTERN_FOREST_MAX_RANGE = 16;
inline constexpr int PATTERN_RESOURCE_DEFAULT_GOLD = 2;
inline constexpr int PATTERN_RESOURCE_DEFAULT_BERRY = 4;
inline constexpr int PATTERN_CROSSING_SIDE_GOLD_COUNT = 5;
inline constexpr int PATTERN_CROSSING_GAP_BERRY_COUNT = 12;
inline constexpr int PATTERN_CROSSING_OUTER_FOREST_RANGE = 9;
inline constexpr int PATTERN_CROSSING_CORNER_FOREST_RANGE = 8;
inline constexpr int PATTERN_COMMONS_CENTER_GOLD_COUNT = 8;
inline constexpr int PATTERN_COMMONS_CENTER_BERRY_COUNT = 12;
inline constexpr int PATTERN_COMMONS_FOREST_RANGE = 8;
inline constexpr int PATTERN_COMMONS_EDGE_FOREST_RANGE = 10;
inline constexpr int PATTERN_RESOURCE_MIN_COUNT = 1;
inline constexpr int PATTERN_RESOURCE_MAX_COUNT = 12;
inline constexpr int PATTERN_RANDOM_GOLD_DEFAULT = 4;
inline constexpr int PATTERN_RANDOM_BERRY_DEFAULT = 4;
inline constexpr int PATTERN_MAKER_WINDOW_WIDTH = 1280;
inline constexpr int PATTERN_MAKER_WINDOW_HEIGHT = 900;
inline constexpr int PATTERN_MAKER_PANEL_WIDTH = 280;
inline constexpr int PATTERN_MAKER_GRID_PAD = 16;
inline constexpr int PATTERN_MAKER_SNAP_CELLS = 1;
inline constexpr int PATTERN_MAKER_UNDO_LIMIT = 64;
inline constexpr int PATTERN_MAKER_DEFAULT_PIECE_SIZE = 4;
inline constexpr int PATTERN_MAKER_BUTTON_HEIGHT = 24;
inline constexpr int PATTERN_MAKER_BUTTON_GAP = 4;
inline constexpr int PATTERN_MAKER_STATUS_HEIGHT = 28;
inline constexpr int PATTERN_MAKER_FONT_SIZE = 14;
inline constexpr std::string_view PATTERN_MAKER_TITLE = "Age of Affinities Pattern Maker";
inline constexpr std::string_view PATTERN_MAKER_DEFAULT_NAME = "untitled";
inline constexpr std::string_view PATTERN_MAKER_FONT_PATH = "C:\\Windows\\Fonts\\consola.ttf";
inline constexpr std::string_view PATTERN_MAKER_FONT_FALLBACK_PATH = "C:\\Windows\\Fonts\\arial.ttf";
inline constexpr int PATTERN_MAKER_DIALOG_WIDTH = 500;
inline constexpr int PATTERN_MAKER_DIALOG_HEIGHT = 300;
inline constexpr int PATTERN_MAKER_DIALOG_PAD = 18;
inline constexpr std::string_view PATTERN_MAKER_CREATE_TITLE = "Create new map";
inline constexpr std::string_view PATTERN_MAKER_LIMIT_PLAYERS_LABEL = "Limit players";
inline constexpr std::string_view PATTERN_MAKER_LIMIT_NO_LABEL = "No";
inline constexpr std::string_view PATTERN_MAKER_LIMIT_YES_LABEL = "Yes";
inline constexpr std::string_view PATTERN_MAKER_MAP_SIZE_LABEL = "Map size";
inline constexpr std::string_view PATTERN_MAKER_MAP_SIZE_ANY_LABEL = "Any";
inline constexpr std::string_view PATTERN_MAKER_CREATE_BUTTON_LABEL = "Create";
inline constexpr std::string_view PATTERN_MAKER_CANCEL_BUTTON_LABEL = "Cancel";
inline constexpr int PATTERN_MAKER_STARTUP_WIDTH = 320;
inline constexpr int PATTERN_MAKER_STARTUP_HEIGHT = 220;
inline constexpr int PATTERN_MAKER_CONFIRM_WIDTH = 360;
inline constexpr int PATTERN_MAKER_CONFIRM_HEIGHT = 160;
inline constexpr int PATTERN_MAKER_STARTUP_BUTTON_WIDTH = 220;
inline constexpr int PATTERN_MAKER_DIALOG_TITLE_BODY_GAP = 36;
inline constexpr int PATTERN_MAKER_DIALOG_ACTION_WIDTH = 120;
inline constexpr int PATTERN_MAKER_DIALOG_ACTION_GAP = 16;
inline constexpr int PATTERN_MAKER_STARTUP_ROW_EXTRA_GAP = 8;
inline constexpr std::string_view PATTERN_MAKER_STARTUP_TITLE = "Pattern Maker";
inline constexpr std::string_view PATTERN_MAKER_STARTUP_NEW_LABEL = "New";
inline constexpr std::string_view PATTERN_MAKER_STARTUP_OPEN_LABEL = "Open";
inline constexpr std::string_view PATTERN_MAKER_STARTUP_CLOSE_LABEL = "Close";
inline constexpr std::string_view PATTERN_MAKER_CONFIRM_CLOSE_TITLE = "Close Pattern Maker";
inline constexpr std::string_view PATTERN_MAKER_CONFIRM_CLOSE_MESSAGE =
    "Are you sure you want to close?";
inline constexpr std::string_view PATTERN_MAKER_CONFIRM_YES_LABEL = "Yes";
inline constexpr std::string_view PATTERN_MAKER_CONFIRM_NO_LABEL = "No";
inline constexpr std::string_view PATTERN_MAKER_PREVIEW_LEGEND =
    "P# start   dark green forest   gold   red berries   blue lake";
inline constexpr std::string_view PATTERN_RAW_FILE_FILTER_DESCRIPTION = "Pattern Maker files";
inline constexpr std::string_view PATTERN_WRONG_KIND_OPEN_LABEL =
    "This file is not a Pattern Maker .raw.pattern.";
inline constexpr std::string_view PATTERN_WRONG_KIND_GAME_LABEL =
    "This file is not a game .pattern export.";
inline constexpr int PATTERN_DOUBLE_CLICK_MS = 400;
inline constexpr int SINGLEPLAYER_OPTION_ROW_COUNT = 7;
inline constexpr int SINGLEPLAYER_COLOR_BUTTON_WIDTH_PX = 110;
inline constexpr int SINGLEPLAYER_KIND_BUTTON_WIDTH_PX = 110;

inline constexpr float SELECTION_PICK_RADIUS_TILES = 0.36F;
inline constexpr float SELECTION_PICK_MIN_RADIUS_PX = 40.0F;
inline constexpr float HUD_HOVER_STICK_SCALE = 1.4F;
inline constexpr float HUD_HOVER_SWITCH_MARGIN_PX = 8.0F;
inline constexpr int SELECTION_BOX_DRAG_THRESHOLD_PX = 6;

inline constexpr int HUD_PIXEL_SCALE = 2;
inline constexpr int HUD_GLYPH_WIDTH = 5;
inline constexpr int HUD_RESOURCE_BAR_GROUP_COUNT = 5;
inline constexpr int HUD_RESOURCE_BAR_VALUE_MAX_CHARS = 9;
inline constexpr int LOCKSTEP_WAITING_TITLE_PIXEL_SCALE = 4;
inline constexpr int HUD_CHAR_SPACING = 1;
inline constexpr int HUD_LINE_SPACING = 4;
inline constexpr float HUD_MARGIN_X = 12.0F;
inline constexpr float HUD_MARGIN_Y = 12.0F;
inline constexpr float HUD_CLIP_Z = 0.0F;
inline constexpr int HUD_POSITION_COMPONENTS = 2;
inline constexpr int HUD_COLOR_VERTEX_COMPONENTS = 6;
inline constexpr int HUD_COLOR_QUAD_TRI_VERTICES = 6;
inline constexpr int HUD_LINE_VERTEX_COUNT = 2;
inline constexpr int HUD_COLOR_BATCH_INITIAL_QUADS = 256;
inline constexpr int HUD_TEXTURED_VERTEX_COMPONENTS = 4;
inline constexpr int HUD_TEXTURED_QUAD_TRI_VERTICES = 6;
inline constexpr int HUD_TEXTURED_BATCH_INITIAL_QUADS = 64;
inline constexpr float HUD_LINE_THICKNESS_PX = 1.0F;
inline constexpr int HUD_FLOATING_LABEL_PAD_PX = 4;
inline constexpr float HUD_TEXT_R = 0.95F;
inline constexpr float HUD_TEXT_G = 0.88F;
inline constexpr float HUD_TEXT_B = 0.55F;
inline constexpr float HUD_PERF_SAMPLE_WINDOW_SECONDS = 0.5F;
inline constexpr float HUD_TPS_SAMPLE_WINDOW_SECONDS = 1.0F;
inline constexpr float HUD_TPS_DISPLAY_SMOOTHING_ALPHA = 0.35F;

inline constexpr float RENDER_FOREST_EXTRUDE = 0.14F;
inline constexpr float RENDER_UNIT_HEIGHT = 0.30F;
inline constexpr float RENDER_UNIT_RADIUS = 0.20F;
inline constexpr int RENDER_CYLINDER_SEGMENTS = 16;
inline constexpr float RENDER_BUILDING_HEIGHT = 0.72F;
inline constexpr float RENDER_ENTITY_BASE_LIFT = 0.04F;
inline constexpr float RENDER_GROUND_HIGHLIGHT_LIFT = 0.012F;
inline constexpr float RENDER_HIGHLIGHT_LINE_WIDTH_TILES = 0.045F;
inline constexpr float RENDER_OCCLUSION_OUTLINE_SCREEN_PX = 2.0F;
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
inline constexpr float RENDER_SELECTION_BOX_R = 0.35F;
inline constexpr float RENDER_SELECTION_BOX_G = 0.95F;
inline constexpr float RENDER_SELECTION_BOX_B = 0.35F;
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
// Movement blocking uses circle radius (adjacent tile centers stay walkable).
// Keep below 0.25 so centers 0.5+ tiles apart do not hard-block mid-path.
inline constexpr float MOVE_UNIT_COLLISION_RADIUS_TILES = 0.24F;
inline constexpr float HITBOX_DEBUG_UNIT_MOVE_R = 0.15F;
inline constexpr float HITBOX_DEBUG_UNIT_MOVE_G = 0.95F;
inline constexpr float HITBOX_DEBUG_UNIT_MOVE_B = 0.85F;
inline constexpr float HITBOX_DEBUG_UNIT_MELEE_R = 0.95F;
inline constexpr float HITBOX_DEBUG_UNIT_MELEE_G = 0.55F;
inline constexpr float HITBOX_DEBUG_UNIT_MELEE_B = 0.15F;
inline constexpr float HITBOX_DEBUG_BUILDING_R = 0.95F;
inline constexpr float HITBOX_DEBUG_BUILDING_G = 0.25F;
inline constexpr float HITBOX_DEBUG_BUILDING_B = 0.35F;
inline constexpr float HITBOX_DEBUG_RESOURCE_R = 0.85F;
inline constexpr float HITBOX_DEBUG_RESOURCE_G = 0.80F;
inline constexpr float HITBOX_DEBUG_RESOURCE_B = 0.20F;
inline constexpr float RENDER_DEBUG_PATH_R = 0.20F;
inline constexpr float RENDER_DEBUG_PATH_G = 0.85F;
inline constexpr float RENDER_DEBUG_PATH_B = 1.0F;
inline constexpr float RENDER_DEBUG_PATH_WAYPOINT_R = 1.0F;
inline constexpr float RENDER_DEBUG_PATH_WAYPOINT_G = 0.90F;
inline constexpr float RENDER_DEBUG_PATH_WAYPOINT_B = 0.20F;
inline constexpr float MELEE_CONTACT_CENTER_DISTANCE_TILES =
    MELEE_UNIT_COLLISION_RADIUS_TILES * 2.0F + 0.02F;
inline constexpr float MELEE_CONTACT_CENTER_DISTANCE_SQ =
    MELEE_CONTACT_CENTER_DISTANCE_TILES * MELEE_CONTACT_CENTER_DISTANCE_TILES;
// Slightly wider than contact so Fixed rounding and symmetric slide still register a strike.
inline constexpr float MELEE_STRIKE_MAX_CENTER_DISTANCE_TILES = 0.55F;
inline constexpr float MELEE_STRIKE_MAX_CENTER_DISTANCE_SQ =
    MELEE_STRIKE_MAX_CENTER_DISTANCE_TILES * MELEE_STRIKE_MAX_CENTER_DISTANCE_TILES;
inline constexpr float MELEE_CONTACT_SLIDE_PER_TICK = 0.12F;

inline constexpr float RENDER_TILE_SPRITE_WIDTH_SCALE = 1.0F;
inline constexpr float RENDER_TREE_SPRITE_WIDTH_SCALE = 1.10F;
inline constexpr float RENDER_TREE_SMALL_SPRITE_WIDTH_SCALE = 0.72F;
inline constexpr float RENDER_TREE_MEDIUM_SPRITE_WIDTH_SCALE = 0.95F;
inline constexpr float RENDER_TREE_LARGE_SPRITE_WIDTH_SCALE = 1.28F;
inline constexpr int TREE_SIZE_VARIANT_COUNT = 3;
inline constexpr float RENDER_TOWN_CENTER_SPRITE_WIDTH_SCALE = 1.18F;
inline constexpr float RENDER_GOLD_MINE_SPRITE_WIDTH_SCALE = 0.72F;
// Sprite draw offsets in grid tiles (+X = SE, +Y = SW on the iso diamond compass).
inline constexpr float RENDER_GOLD_MINE_SPRITE_OFFSET_X = 0.5F;
inline constexpr float RENDER_GOLD_MINE_SPRITE_OFFSET_Y = 0.5F;
inline constexpr float RENDER_TOWN_CENTER_SPRITE_OFFSET_X = 3.0F;
inline constexpr float RENDER_TOWN_CENTER_SPRITE_OFFSET_Y = 3.0F;
inline constexpr float RENDER_HOUSE_SPRITE_OFFSET_X = 0.8F;
inline constexpr float RENDER_HOUSE_SPRITE_OFFSET_Y = 0.8F;
// Lumberjack: pull SE/SW from house offsets to correct NE/NW drift.
inline constexpr float RENDER_LUMBERJACK_SPRITE_OFFSET_X = 1.25F;
inline constexpr float RENDER_LUMBERJACK_SPRITE_OFFSET_Y = 1.1F;
// Extractor: correct 0.4 S + 0.1 W drift (lower SE+SW, slight SE bias = east).
inline constexpr float RENDER_EXTRACTOR_SPRITE_OFFSET_X = 0.25F;
inline constexpr float RENDER_EXTRACTOR_SPRITE_OFFSET_Y = 0.15F;
inline constexpr float RENDER_EXTRACTOR_SPRITE_WIDTH_SCALE = 0.62F;
// Mana lake: correct 0.2 N drift (raise SE+SW offsets).
inline constexpr float RENDER_MANA_LAKE_SPRITE_OFFSET_X = 1.05F;
inline constexpr float RENDER_MANA_LAKE_SPRITE_OFFSET_Y = 1.05F;
inline constexpr float RENDER_MANA_LAKE_SPRITE_WIDTH_SCALE = 1.6F;
// Lakes sit on the ground: sort with terrain props so buildings/units draw above them.
inline constexpr float RENDER_MANA_LAKE_SORT_BIAS = 0.02F;
inline constexpr int MANA_LAKE_ANIMATION_FRAME_COUNT = 8;
inline constexpr std::array<int, MANA_LAKE_ANIMATION_FRAME_COUNT>
    MANA_LAKE_ANIMATION_FRAME_DURATIONS_MS = {100, 100, 150, 75, 150, 100, 100, 500};
inline constexpr float RENDER_TREE_SPRITE_OFFSET_X = 0.2F;
inline constexpr float RENDER_TREE_SPRITE_OFFSET_Y = 0.2F;
inline constexpr float RENDER_OAK_LARGE_SPRITE_OFFSET_X = 0.5F;
inline constexpr float RENDER_OAK_LARGE_SPRITE_OFFSET_Y = 0.3F;
inline constexpr float RENDER_OAK_MEDIUM_SPRITE_OFFSET_X = 0.7F;
inline constexpr float RENDER_OAK_MEDIUM_SPRITE_OFFSET_Y = 0.7F;
inline constexpr float RENDER_OAK_SMALL_SPRITE_OFFSET_X = 0.275F;
inline constexpr float RENDER_OAK_SMALL_SPRITE_OFFSET_Y = 0.3F;
inline constexpr float RENDER_PINES_SMALL_SPRITE_OFFSET_X = 0.275F;
inline constexpr float RENDER_PINES_SMALL_SPRITE_OFFSET_Y = 0.285F;
inline constexpr float RENDER_PINES_MEDIUM_SPRITE_OFFSET_X = 0.45F;
inline constexpr float RENDER_PINES_MEDIUM_SPRITE_OFFSET_Y = 0.5F;
inline constexpr float RENDER_PINES_LARGE_SPRITE_OFFSET_X = 0.2F;
inline constexpr float RENDER_PINES_LARGE_SPRITE_OFFSET_Y = 0.2F;
inline constexpr float RENDER_BERRY_SPRITE_OFFSET_X = 0.5F;
inline constexpr float RENDER_BERRY_SPRITE_OFFSET_Y = 0.5F;
inline constexpr int BUILDING_UNIT_SPAWN_MAX_RING = 8;
inline constexpr float RENDER_TREE_SORT_BIAS = 0.10F;
inline constexpr float RENDER_BUILDING_SORT_BIAS = 0.05F;
inline constexpr float RENDER_UNIT_SORT_BIAS = 0.08F;
inline constexpr float RENDER_TREE_OCCLUSION_SCISSOR_SCALE = 1.05F;
inline constexpr float RENDER_BUILDING_OCCLUSION_SCISSOR_SCALE = 1.15F;
inline constexpr int RENDER_OCCLUSION_PROBE_RADIUS = 1;
inline constexpr float RENDER_GROUND_SPRITE_SORT_Y = 0.0F;
inline constexpr float RENDER_TREE_SPRITE_SORT_Y = 0.35F;
inline constexpr float RENDER_BUILDING_SPRITE_SORT_Y = 0.45F;
inline constexpr float RENDER_TEXTURED_GRID_LINE_LIFT = 0.12F;
inline constexpr float RENDER_GRASS_UV_LEFT = 0.0F;
inline constexpr float RENDER_GRASS_UV_TOP = 0.0F;
inline constexpr float RENDER_GRASS_UV_RIGHT = 1.0F;
inline constexpr float RENDER_GRASS_UV_BOTTOM = 1.0F;

inline constexpr float RENDER_GRID_LINE_R = 0.12F;
inline constexpr float RENDER_GRID_LINE_G = 0.17F;
inline constexpr float RENDER_GRID_LINE_B = 0.10F;
inline constexpr float RENDER_GRID_LINE_WIDTH = 0.035F;
inline constexpr float RENDER_GRID_LINE_LIFT = 0.03F;

inline constexpr std::string_view EARTH_CIV_ID = "earth";
inline constexpr std::string_view WORKER_UNIT_ID = "worker";
inline constexpr std::string_view MILITIA_UNIT_ID = "militia";
inline constexpr std::string_view TOWN_CENTER_BUILDING_ID = "town_center";
inline constexpr std::string_view HOUSE_BUILDING_ID = "house";
inline constexpr std::string_view LUMBERJACK_BUILDING_ID = "lumberjack";
inline constexpr int LUMBERJACK_FOOTPRINT_TILES = 2;
inline constexpr int LUMBERJACK_BUILD_WOOD_COST = 65;
inline constexpr std::string_view EXTRACTOR_BUILDING_ID = "extractor";
inline constexpr int EXTRACTOR_FOOTPRINT_TILES = 2;
inline constexpr int EXTRACTOR_BUILD_WOOD_COST = 75;
inline constexpr int EXTRACTOR_BUILD_MONEY_COST = 25;
inline constexpr int EXTRACTOR_MAX_HP = 200;
inline constexpr std::string_view MANA_LAKE_BUILDING_ID = "mana_lake";
inline constexpr int MANA_LAKE_FOOTPRINT_TILES = 2;
inline constexpr int MANA_LAKE_PER_PLAYER_COUNT = 1;
// Chebyshev ring band (inclusive) searched around each town center anchor.
inline constexpr int MANA_LAKE_MIN_RING_FROM_TOWN_CENTER = 13;
inline constexpr int MANA_LAKE_MAX_RING_FROM_TOWN_CENTER = 17;
inline constexpr int MANA_LAKE_RESOURCE_KEEP_TILES = 4;
inline constexpr std::string_view FOREST_PATCH_RESOURCE_ID = "forest_patch";
inline constexpr std::string_view BERRY_BUSH_RESOURCE_ID = "berry_bush";
inline constexpr std::string_view BLUEBERRY_BUSH_RESOURCE_ID = "blueberry_bush";
inline constexpr std::string_view GOLD_MINE_RESOURCE_ID = "gold_mine";

inline constexpr int PLAYER_MANA_MAX = 50;
inline constexpr int CIVIL_POPULATION_CAP_BASE = 0;
inline constexpr int CIVIL_POPULATION_CAP_PER_TOWN_CENTER = 5;
inline constexpr int CIVIL_POPULATION_CAP_PER_HOUSE = 3;
inline constexpr int CIVIL_POPULATION_CAP_MAX = 200;
inline constexpr int CIVIL_POPULATION_MAP_CAP_DEFAULT = 15;
inline constexpr int CIVIL_POPULATION_MAP_CAP_OPTION_A = 15;
inline constexpr int CIVIL_POPULATION_MAP_CAP_OPTION_B = 25;
inline constexpr int CIVIL_POPULATION_MAP_CAP_OPTION_C = 35;
inline constexpr int EXTRACTOR_MANA_GEN_AMOUNT = 1;
inline constexpr int EXTRACTOR_MANA_GEN_INTERVAL_SECONDS = 5;
inline constexpr int EXTRACTOR_MANA_GEN_INTERVAL_TICKS =
    SIM_TICKS_PER_SECOND * EXTRACTOR_MANA_GEN_INTERVAL_SECONDS;
// Every completed extractor raises the player's mana ceiling by this much.
inline constexpr int MANA_CAP_PER_EXTRACTOR = 10;
inline constexpr int TOWN_CENTER_BUILD_WOOD_COST = 250;
inline constexpr int HOUSE_BUILD_WOOD_COST = 30;
inline constexpr int BERRY_BUSH_FOOD_CAPACITY = 75;
inline constexpr int BERRY_PATCH_TILE_COUNT = 7;
inline constexpr int BERRY_PATCH_MIN_RING = 8;
inline constexpr int BERRY_PATCH_MAX_RING = 11;
inline constexpr int FOREST_NEAR_TC_MIN_COUNT = 5;
inline constexpr int FOREST_NEAR_TC_MAX_COUNT = 8;
inline constexpr int FOREST_NEAR_TC_MIN_RING = 9;
inline constexpr int FOREST_NEAR_TC_MAX_RING = 11;
inline constexpr int FOREST_GROVE_TILE_COUNT = 110;
inline constexpr int FOREST_GROVE_COUNT_PER_PLAYER = 2;
inline constexpr int FOREST_GROVE_MIN_RING = 14;
inline constexpr int FOREST_GROVE_MAX_RING = 18;
inline constexpr int FOREST_GROVE_GROW_RING_SLACK = 4;
inline constexpr int PLAYER_START_GRASS_KEEP_RING = 10;
inline constexpr int PINE_GROVE_TILE_COUNT = 180;
inline constexpr int PINE_GROVE_COUNT_TWO_PLAYER = 2;
inline constexpr int PINE_GROVE_COUNT_MULTI_PLAYER = 2;
inline constexpr int BIOME_SEED_JITTER_TILES = 3;
inline constexpr int BIOME_NOISE_CELL_SIZE = 8;
inline constexpr float BIOME_BORDER_WOBBLE = 48.0F;
inline constexpr int BIOME_SMOOTH_PASSES = 2;
inline constexpr int BIOME_BLEND_RADIUS_TILES = 1;
inline constexpr float BIOME_BLEND_SIGMA = 0.65F;
inline constexpr float BIOME_BLEND_MIN_CORNER_ALPHA = 0.02F;
inline constexpr std::uint64_t FNV1A_OFFSET_BASIS = 1469598103934665603ULL;
inline constexpr std::uint64_t FNV1A_PRIME = 1099511628211ULL;
inline constexpr int GROUND_TYPE_COUNT = 3;
inline constexpr std::uint32_t BIOME_NOISE_FRACTION_MASK = 0xFFFFU;
inline constexpr float BIOME_SMOOTHSTEP_MUL = 2.0F;
inline constexpr float BIOME_SMOOTHSTEP_ADD = 3.0F;
inline constexpr float BIOME_NOISE_SIGNED_SCALE = 2.0F;
inline constexpr float BIOME_NOISE_SIGNED_BIAS = 1.0F;
inline constexpr int GOLD_MINE_MIN_RING = 8;
inline constexpr int GOLD_MINE_MAX_RING = 12;
inline constexpr std::uint32_t MAP_GEN_LAYOUT_SALT = 0xC2B2AE35U;
inline constexpr int SNOW_BIOME_2P_SEED_A_X = 52;
inline constexpr int SNOW_BIOME_2P_SEED_A_Y = 10;
inline constexpr int SNOW_BIOME_2P_SEED_B_X = 58;
inline constexpr int SNOW_BIOME_2P_SEED_B_Y = 22;
inline constexpr int SAND_BIOME_2P_SEED_A_X = 10;
inline constexpr int SAND_BIOME_2P_SEED_A_Y = 52;
inline constexpr int SAND_BIOME_2P_SEED_B_X = 22;
inline constexpr int SAND_BIOME_2P_SEED_B_Y = 58;
inline constexpr int GRASS_BIOME_2P_SEED_A_X = 12;
inline constexpr int GRASS_BIOME_2P_SEED_A_Y = 12;
inline constexpr int GRASS_BIOME_2P_SEED_B_X = 52;
inline constexpr int GRASS_BIOME_2P_SEED_B_Y = 52;
inline constexpr int SNOW_BIOME_MULTI_SEED_X = 32;
inline constexpr int SNOW_BIOME_MULTI_SEED_Y = 32;
inline constexpr int SNOW_BIOME_MULTI_SEED_B_X = 28;
inline constexpr int SNOW_BIOME_MULTI_SEED_B_Y = 36;
inline constexpr int SAND_BIOME_MULTI_SEED_X = 18;
inline constexpr int SAND_BIOME_MULTI_SEED_Y = 32;
inline constexpr int SAND_BIOME_MULTI_SEED_B_X = 46;
inline constexpr int SAND_BIOME_MULTI_SEED_B_Y = 32;
inline constexpr int MAP_GEN_SCATTER_MAX_ATTEMPTS = 96;
inline constexpr int BLUEBERRY_CLUSTER_SIZE = 3;
inline constexpr int BLUEBERRY_CLUSTER_COUNT = 3;
inline constexpr int BLUEBERRY_MIN_DISTANCE_FROM_BASE = 18;
inline constexpr int BLUEBERRY_PLACEMENT_MAX_ATTEMPTS = 512;
inline constexpr int GOLD_MINE_MONEY_CAPACITY = 450;
inline constexpr int GOLD_MINE_NEAR_BASE_COUNT = 3;
inline constexpr int GOLD_MINE_VARIANT_COUNT = 4;
inline constexpr int WORKER_FOOD_COST = 45;
inline constexpr int MILITIA_FOOD_COST = 65;
inline constexpr int MILITIA_MONEY_COST = 35;
inline constexpr int AI_THINK_INTERVAL_TICKS = 4;
inline constexpr int AI_WOOD_WORKERS_MIN = 2;
inline constexpr int AI_FOOD_WORKERS_MIN = 1;
inline constexpr int AI_GOLD_WORKERS_MIN = 1;
inline constexpr int AI_HOUSE_TARGET = 2;
// Min Chebyshev gap from any house tile to the TC footprint (1 walkable stand ring).
inline constexpr int AI_HOUSE_TC_MIN_SEPARATION_TILES = 2;
inline constexpr int AI_MILITIA_WAVE_SIZE = 4;
inline constexpr int AI_SCOUT_COUNT = 1;
inline constexpr int AI_WOOD_STOCKPILE_TARGET = 90;
inline constexpr int HUD_COMMAND_PANEL_KEY_PRESS_TTL_MS = 120;
// Construction: 9 HP per hit at ~1.34 hits/s (20 TPS => 15-tick interval).
inline constexpr int WORKER_BUILD_HP_PER_HIT = 9;
inline constexpr float WORKER_BUILD_HITS_PER_SECOND = 1.34F;
inline constexpr int WORKER_BUILD_HIT_INTERVAL_TICKS = 15;
inline constexpr int DEFAULT_STARTING_STOCKPILE_FOOD = 50;
inline constexpr int DEFAULT_STARTING_STOCKPILE_WOOD = 60;
inline constexpr int DEFAULT_STARTING_STOCKPILE_MONEY = 35;
inline constexpr int DEFAULT_STARTING_STOCKPILE_MANA = 0;

inline constexpr float HUD_BOTTOM_PANEL_WIDTH_FRACTION = 0.16F;
inline constexpr float HUD_BOTTOM_PANEL_ASPECT_WIDTH = 5.0F;
inline constexpr float HUD_BOTTOM_PANEL_ASPECT_HEIGHT = 3.0F;
inline constexpr int HUD_BOTTOM_PANEL_COLUMNS = 5;
inline constexpr int HUD_BOTTOM_PANEL_ROWS = 3;
inline constexpr int HUD_OPTIONS_FRAME_MARGIN_PX = 0;
inline constexpr int HUD_OPTIONS_FRAME_PADDING_PX = 4;
inline constexpr int HUD_OPTIONS_BUTTON_GAP_PX = 3;
inline constexpr int HUD_OPTIONS_LABEL_PADDING_Y_PX = 4;
// Legacy aliases used by older call sites.
inline constexpr float HUD_OPTIONS_FRAME_WIDTH_FRACTION = HUD_BOTTOM_PANEL_WIDTH_FRACTION;
inline constexpr float HUD_OPTIONS_FRAME_ASPECT_WIDTH = HUD_BOTTOM_PANEL_ASPECT_WIDTH;
inline constexpr float HUD_OPTIONS_FRAME_ASPECT_HEIGHT = HUD_BOTTOM_PANEL_ASPECT_HEIGHT;
inline constexpr float HUD_OPTIONS_FRAME_R = 0.06F;
inline constexpr float HUD_OPTIONS_FRAME_G = 0.07F;
inline constexpr float HUD_OPTIONS_FRAME_B = 0.08F;
inline constexpr float HUD_OPTIONS_FRAME_BORDER_R = 0.55F;
inline constexpr float HUD_OPTIONS_FRAME_BORDER_G = 0.55F;
inline constexpr float HUD_OPTIONS_FRAME_BORDER_B = 0.55F;
inline constexpr float HUD_OPTIONS_BUTTON_R = 0.22F;
inline constexpr float HUD_OPTIONS_BUTTON_G = 0.28F;
inline constexpr float HUD_OPTIONS_BUTTON_B = 0.18F;
inline constexpr float RENDER_CONSTRUCTION_HIGHLIGHT_R = 0.25F;
inline constexpr float RENDER_CONSTRUCTION_HIGHLIGHT_G = 0.55F;
inline constexpr float RENDER_CONSTRUCTION_HIGHLIGHT_B = 0.95F;
inline constexpr float RENDER_GHOST_BUILDING_ALPHA = 0.55F;
// Under-construction buildings reveal only their occupied tiles, and only after
// construction has started (current HP greater than this foundation value).
inline constexpr int CONSTRUCTION_VISION_ACTIVE_MIN_HP = 1;
inline constexpr int CONSTRUCTION_VISION_FOOTPRINT_PADDING_TILES = 0;
// Radius from a footprint tile center that covers that tile only (half-diagonal).
inline constexpr float CONSTRUCTION_VISION_PER_TILE_RADIUS = 0.70710678F;
inline constexpr int CHAT_MAX_MESSAGE_LENGTH = 80;
inline constexpr int CHAT_INPUT_VISIBLE_CHARS = 80;
inline constexpr int CHAT_MAX_VISIBLE_LINES = 8;
inline constexpr int CHAT_MESSAGE_TTL_MS = 10000;
inline constexpr int CHAT_SYSTEM_MESSAGE_TTL_MS = 20000;
inline constexpr std::uint8_t CHAT_SYSTEM_PLAYER_SLOT = 255U;
inline constexpr float CHAT_FRAME_WIDTH_FRACTION = 0.28F;
inline constexpr int CHAT_FRAME_MARGIN_TOP_PX = 8;
inline constexpr float CHAT_INPUT_WIDTH_FRACTION = 0.55F;
inline constexpr int CHAT_INPUT_HEIGHT_PX = 28;
inline constexpr int CURSOR_HOTSPOT_X = 0;
inline constexpr int CURSOR_HOTSPOT_Y = 0;
inline constexpr int CURSOR_SHAPE_FOLDER_QUESTION = 1;
inline constexpr int CURSOR_SHAPE_FOLDER_EXCLAMATION = 2;
inline constexpr int CURSOR_SHAPE_FOLDER_CHECK = 4;
inline constexpr int CURSOR_SHAPE_FOLDER_CROSS = 5;
inline constexpr int CURSOR_SHAPE_FOLDER_RALLY = 13;
inline constexpr int CURSOR_SHAPE_FOLDER_BLOCKED = 20;
inline constexpr int CURSOR_SHAPE_FOLDER_ATTACK = 21;
inline constexpr int CURSOR_SHAPE_FOLDER_ATTACK_RESTRICTED = 22;
inline constexpr int CURSOR_SHAPE_FOLDER_RESTRICTED = 23;
inline constexpr int CURSOR_SHAPE_FOLDER_TARGET = 27;
inline constexpr int CURSOR_SHAPE_FOLDER_NORMAL = 28;
// Cyan-looking Blue PNGs (file 10) get shifted toward true blue.
inline constexpr float CURSOR_BLUEISH_RED_SCALE = 0.75F;
inline constexpr float CURSOR_BLUEISH_GREEN_SCALE = 0.45F;
inline constexpr float CURSOR_BLUEISH_BLUE_SCALE = 1.20F;
// Cyan player color is synthesized from the Blue PNG of each shape.
inline constexpr float CURSOR_CYAN_RED_SCALE = 0.30F;
inline constexpr float CURSOR_CYAN_GREEN_SCALE = 1.05F;
inline constexpr float CURSOR_CYAN_BLUE_SCALE = 1.10F;
inline constexpr float HUD_UNAFFORDABLE_R = 0.85F;
inline constexpr float HUD_UNAFFORDABLE_G = 0.20F;
inline constexpr float HUD_UNAFFORDABLE_B = 0.18F;

inline constexpr int MINIMAP_CONTENT_PADDING_PX = 3;
inline constexpr int MINIMAP_CAMERA_BOX_LINE_PX = 1;
inline constexpr int MINIMAP_TEXTURE_MAX_EDGE_PX = 256;
inline constexpr float MINIMAP_FOG_UNEXPLORED_R = 0.02F;
inline constexpr float MINIMAP_FOG_UNEXPLORED_G = 0.02F;
inline constexpr float MINIMAP_FOG_UNEXPLORED_B = 0.03F;
inline constexpr float MINIMAP_FOG_EXPLORED_R = 0.14F;
inline constexpr float MINIMAP_FOG_EXPLORED_G = 0.16F;
inline constexpr float MINIMAP_FOG_EXPLORED_B = 0.12F;
inline constexpr float MINIMAP_FOG_VISIBLE_GRASS_R = 0.28F;
inline constexpr float MINIMAP_FOG_VISIBLE_GRASS_G = 0.42F;
inline constexpr float MINIMAP_FOG_VISIBLE_GRASS_B = 0.22F;
inline constexpr float MINIMAP_FOG_VISIBLE_FOREST_R = 0.16F;
inline constexpr float MINIMAP_FOG_VISIBLE_FOREST_G = 0.30F;
inline constexpr float MINIMAP_FOG_VISIBLE_FOREST_B = 0.14F;
inline constexpr float MINIMAP_FOG_VISIBLE_SNOW_R = 0.78F;
inline constexpr float MINIMAP_FOG_VISIBLE_SNOW_G = 0.84F;
inline constexpr float MINIMAP_FOG_VISIBLE_SNOW_B = 0.90F;
inline constexpr float MINIMAP_FOG_VISIBLE_SAND_R = 0.72F;
inline constexpr float MINIMAP_FOG_VISIBLE_SAND_G = 0.62F;
inline constexpr float MINIMAP_FOG_VISIBLE_SAND_B = 0.38F;
inline constexpr float MINIMAP_FOG_VISIBLE_GOLD_R = 0.86F;
inline constexpr float MINIMAP_FOG_VISIBLE_GOLD_G = 0.71F;
inline constexpr float MINIMAP_FOG_VISIBLE_GOLD_B = 0.16F;
inline constexpr float MINIMAP_FOG_VISIBLE_BERRY_R = 0.63F;
inline constexpr float MINIMAP_FOG_VISIBLE_BERRY_G = 0.16F;
inline constexpr float MINIMAP_FOG_VISIBLE_BERRY_B = 0.27F;
inline constexpr float MINIMAP_FOG_EXPLORED_DIM = 0.62F;
inline constexpr float MINIMAP_CAMERA_BOX_R = 1.0F;
inline constexpr float MINIMAP_CAMERA_BOX_G = 1.0F;
inline constexpr float MINIMAP_CAMERA_BOX_B = 1.0F;
inline constexpr int MINIMAP_UNIT_MARKER_MIN_PX = 2;
inline constexpr int MINIMAP_BUILDING_MARKER_MIN_PX = 3;

inline constexpr std::string_view SFX_SWORD_CLASH_RELATIVE_PATH = "sfx/Weapons/sword_clash_2.wav";
inline constexpr std::string_view SFX_KICK_RELATIVE_PATH = "sfx/Combat and Gore/kick.wav";
inline constexpr std::string_view SFX_WOODEN_CLICK_RELATIVE_PATH =
    "sfx/JDSherbert - Wooden UI SFX Pack (FREE)/Mono/wav (HD)/JDSherbert - Wooden UI SFX Pack - Confirm - 1.wav";
inline constexpr std::string_view SFX_BUILDING_RELATIVE_PATH = "sfx/Cringemarine/building.wav";
inline constexpr std::string_view SFX_CHOPPING_RELATIVE_PATH = "sfx/Cringemarine/chopping.wav";
inline constexpr std::string_view SFX_GATHERING_RELATIVE_PATH = "sfx/Cringemarine/gathering.wav";
inline constexpr std::string_view SFX_MINING_RELATIVE_PATH = "sfx/Cringemarine/mining.wav";
inline constexpr std::string_view SFX_DEATH_1_RELATIVE_PATH = "sfx/Cringemarine/death1.wav";
inline constexpr std::string_view SFX_DEATH_2_RELATIVE_PATH = "sfx/Cringemarine/death2.wav";
inline constexpr std::string_view SFX_DEATH_3_RELATIVE_PATH = "sfx/Cringemarine/death3.wav";
inline constexpr std::string_view SFX_EARTH_RELATIVE_PATH = "sfx/Cringemarine/earth.wav";
inline constexpr std::string_view SFX_WHAT_RELATIVE_PATH = "sfx/Cringemarine/what.wav";
inline constexpr std::string_view SFX_YES_RELATIVE_PATH = "sfx/Cringemarine/yes.wav";
inline constexpr std::string_view SFX_NO_RELATIVE_PATH = "sfx/Cringemarine/no.wav";
inline constexpr std::string_view SFX_OKAY_RELATIVE_PATH = "sfx/Cringemarine/okay.wav";
inline constexpr std::string_view SFX_ILL_DO_IT_RELATIVE_PATH = "sfx/Cringemarine/i-ll-do-it.wav";
inline constexpr std::string_view SFX_MOVING_HERE_RELATIVE_PATH = "sfx/Cringemarine/moving-here.wav";
inline constexpr std::string_view MUSIC_TRACK_1_RELATIVE_PATH = "music/game_music1.wav";
inline constexpr std::string_view MUSIC_TRACK_2_RELATIVE_PATH = "music/game_music2.wav";
inline constexpr std::string_view MUSIC_TRACK_3_RELATIVE_PATH = "music/game_music3.wav";
inline constexpr std::string_view MUSIC_MAIN_MENU_RELATIVE_PATH = "music/main_menu_theme.wav";
inline constexpr float AUDIO_SFX_VOLUME = 100.0F;
inline constexpr float AUDIO_MUSIC_VOLUME = 100.0F;
inline constexpr float AUDIO_MASTER_VOLUME = 100.0F;
inline constexpr std::string_view SETTINGS_FILE_NAME = "settings.json";
inline constexpr int SETTINGS_FILE_VERSION = 1;
inline constexpr float AUDIO_VOLUME_MIN = 0.0F;
inline constexpr float AUDIO_VOLUME_MAX = 100.0F;
inline constexpr int AUDIO_MAX_CONCURRENT_SFX = 16;

inline constexpr float HUD_MENU_BUTTON_WIDTH_FRACTION = 0.08F;
inline constexpr float HUD_MENU_BUTTON_HEIGHT_FRACTION = 0.045F;
inline constexpr int HUD_MENU_BUTTON_MARGIN_PX = 10;
inline constexpr float HUD_GAME_MENU_WIDTH_FRACTION = 0.28F;
inline constexpr float HUD_GAME_MENU_HEIGHT_FRACTION = 0.52F;
inline constexpr float HUD_SETTINGS_WIDTH_FRACTION = 0.36F;
inline constexpr float HUD_SETTINGS_HEIGHT_FRACTION = 0.56F;
inline constexpr float HUD_SAVE_LOAD_WIDTH_FRACTION = 0.42F;
inline constexpr float HUD_SAVE_LOAD_HEIGHT_FRACTION = 0.62F;
inline constexpr float HUD_SAVE_CONFIRM_WIDTH_FRACTION = 0.34F;
inline constexpr float HUD_SAVE_CONFIRM_HEIGHT_FRACTION = 0.22F;
inline constexpr int HUD_GAME_MENU_BUTTON_COUNT = 6;
inline constexpr int HUD_SAVE_LOAD_VISIBLE_ROWS = 8;
inline constexpr int HUD_SAVE_LOAD_MAX_FILENAME_LENGTH = 48;
inline constexpr float HUD_SAVE_LOAD_INPUT_HEIGHT_PX = 28.0F;
inline constexpr float HUD_SAVE_LOAD_ROW_HEIGHT_PX = 26.0F;
inline constexpr float HUD_SAVE_LOAD_ACTION_WIDTH_PX = 100.0F;
inline constexpr float HUD_SAVE_LOAD_ACTION_HEIGHT_PX = 28.0F;
inline constexpr int AUTOSAVE_INTERVAL_MS = 5 * 60 * 1000;
inline constexpr int HUD_SETTINGS_TAB_COUNT = 3;
inline constexpr float HUD_VOLUME_SLIDER_HEIGHT_PX = 14.0F;
inline constexpr float HUD_VOLUME_SLIDER_TRACK_R = 0.18F;
inline constexpr float HUD_VOLUME_SLIDER_TRACK_G = 0.18F;
inline constexpr float HUD_VOLUME_SLIDER_TRACK_B = 0.20F;
inline constexpr float HUD_VOLUME_SLIDER_FILL_R = 0.35F;
inline constexpr float HUD_VOLUME_SLIDER_FILL_G = 0.55F;
inline constexpr float HUD_VOLUME_SLIDER_FILL_B = 0.75F;
inline constexpr float HUD_SETTINGS_TAB_HEIGHT_PX = 28.0F;
inline constexpr float HUD_SETTINGS_ROW_HEIGHT_PX = 32.0F;
inline constexpr float HUD_SETTINGS_BACK_WIDTH_PX = 90.0F;
inline constexpr float HUD_SETTINGS_BACK_HEIGHT_PX = 28.0F;
inline constexpr float HUD_SETTINGS_LABEL_GAP_PX = 22.0F;
inline constexpr float HUD_SETTINGS_SLIDER_ROW_EXTRA_PX = 18.0F;
inline constexpr float HUD_SETTINGS_ACTIVE_TAB_BRIGHTEN = 1.35F;
inline constexpr float HUD_MENU_DISABLED_DIM = 0.45F;
inline constexpr float HUD_MENU_SCRIM_R = 0.0F;
inline constexpr float HUD_MENU_SCRIM_G = 0.0F;
inline constexpr float HUD_MENU_SCRIM_B = 0.0F;
inline constexpr float HUD_MENU_SCRIM_A = 0.45F;
inline constexpr int AUDIO_DEATH_VARIANT_COUNT = 3;
inline constexpr int AUDIO_SELECT_ACK_VARIANT_COUNT = 3;
inline constexpr int AUDIO_MOVE_ACK_VARIANT_COUNT = 3;
inline constexpr std::uint8_t SINGLEPLAYER_HUMAN_PLAYER_SLOT = 0U;
inline constexpr std::uint8_t SINGLEPLAYER_AI_PLAYER_SLOT = 1U;
inline constexpr std::uint8_t SINGLEPLAYER_PLAYER_COUNT = 2U;
inline constexpr std::string_view SCENARIOS_DIRECTORY_NAME = "scenarios";
inline constexpr std::string_view SCENARIO_FILE_EXTENSION = ".scenario";
inline constexpr std::string_view SCENARIO_FILE_FILTER_PATTERN = "*.scenario";
inline constexpr std::string_view PATTERN_CONSTRAINT_PLAYERS_LABEL =
    "This pattern does not allow the current player count.";
inline constexpr std::string_view PATTERN_CONSTRAINT_FIXED_PLAYERS_PREFIX =
    "This pattern requires ";
inline constexpr std::string_view PATTERN_CONSTRAINT_MIN_PLAYERS_PREFIX =
    "This pattern requires at least ";
inline constexpr std::string_view PATTERN_CONSTRAINT_MAX_PLAYERS_PREFIX =
    "This pattern allows at most ";
inline constexpr std::string_view PATTERN_CONSTRAINT_PLAYERS_MID =
    " players (lobby has ";
inline constexpr std::string_view PATTERN_CONSTRAINT_PLAYERS_SUFFIX = ").";
inline constexpr std::string_view PATTERN_CONSTRAINT_LOAD_FAILED_LABEL =
    "Could not load map pattern.";
inline constexpr std::string_view PATTERN_CONSTRAINT_MISSING_LABEL =
    "Select a map pattern file first.";
inline constexpr std::string_view PATTERN_PLAYER_COUNT_LOCKED_LABEL =
    "Player count is locked by the selected map pattern.";
inline constexpr std::string_view PATTERN_MAP_SIZE_LOCKED_LABEL =
    "Map size is locked by the selected map pattern.";
inline constexpr std::string_view PATTERN_MAP_SIZE_PREFIX = "Map Size: ";
inline constexpr std::string_view PATTERN_FILE_FILTER_DESCRIPTION = "Game map pattern files";
inline constexpr std::string_view SINGLEPLAYER_GAME_STYLE_PREFIX = "Game Style: ";
inline constexpr std::string_view SINGLEPLAYER_GAME_STYLE_RANDOM_LABEL = "Random game";
inline constexpr std::string_view SINGLEPLAYER_GAME_STYLE_SCENARIOS_LABEL = "Scenarios";
inline constexpr std::string_view SINGLEPLAYER_SELECT_SCENARIO_PREFIX = "Scenario: ";
inline constexpr std::string_view SINGLEPLAYER_SELECT_SCENARIO_EMPTY_LABEL = "Select Scenario";
inline constexpr std::string_view SINGLEPLAYER_MAP_PATTERN_PREFIX = "Map Pattern: ";
inline constexpr std::string_view SINGLEPLAYER_FOG_PREFIX = "Fog of War: ";
inline constexpr std::string_view SINGLEPLAYER_FOG_ENABLED_LABEL = "Enabled";
inline constexpr std::string_view SINGLEPLAYER_FOG_EXPLORED_LABEL = "Explored";
inline constexpr std::string_view SINGLEPLAYER_FOG_DISABLED_LABEL = "Disabled";
inline constexpr std::string_view SINGLEPLAYER_CIVIL_CAP_PREFIX = "Civil Cap: ";
inline constexpr std::string_view SINGLEPLAYER_CHEATS_PREFIX = "Use Cheats: ";
inline constexpr std::string_view VICTORY_CONDITION_PREFIX = "Victory: ";
inline constexpr std::string_view VICTORY_CONDITION_NORMAL_LABEL = "Normal game";
inline constexpr std::uint8_t MATCH_WINNER_NONE = 255U;
inline constexpr std::uint64_t MATCH_OUTCOME_MIN_TICK = 1U;
inline constexpr float HUD_MATCH_RESULT_WIDTH_FRACTION = 0.42F;
inline constexpr float HUD_MATCH_RESULT_HEIGHT_FRACTION = 0.62F;
inline constexpr int HUD_MATCH_RESULT_TITLE_PIXEL_SCALE = 3;
inline constexpr float HUD_MATCH_RESULT_LINE_GAP_PX = 8.0F;
inline constexpr float HUD_MATCH_RESULT_EXIT_WIDTH_PX = 170.0F;
inline constexpr float HUD_VICTORY_TITLE_R = 0.35F;
inline constexpr float HUD_VICTORY_TITLE_G = 0.75F;
inline constexpr float HUD_VICTORY_TITLE_B = 0.45F;
inline constexpr float HUD_DEFEAT_TITLE_R = 0.85F;
inline constexpr float HUD_DEFEAT_TITLE_G = 0.25F;
inline constexpr float HUD_DEFEAT_TITLE_B = 0.25F;
inline constexpr std::string_view SINGLEPLAYER_CHEATS_DISABLED_LABEL = "Disabled";
inline constexpr std::string_view SINGLEPLAYER_CHEATS_ENABLED_LABEL = "Enabled";
inline constexpr std::string_view SINGLEPLAYER_SLOT_HOST_LABEL = "You";
inline constexpr std::string_view SINGLEPLAYER_SLOT_HOST_KIND_LABEL = "Host";
inline constexpr std::string_view SINGLEPLAYER_SLOT_SPECTATOR_LABEL = "Spectator";
inline constexpr std::string_view SINGLEPLAYER_SLOT_AI_LABEL = "AI";
inline constexpr std::string_view SINGLEPLAYER_SLOT_ENABLED_LABEL = "Enabled";
inline constexpr std::string_view SINGLEPLAYER_SLOT_DISABLED_LABEL = "Disabled";
inline constexpr std::string_view SINGLEPLAYER_PATTERN_PICKER_TITLE = "Map Pattern";
inline constexpr std::string_view SINGLEPLAYER_SCENARIO_FILTER_DESCRIPTION = "Scenario files";
inline constexpr int NATIVE_FILE_DIALOG_PATH_CHARS = 1024;
inline constexpr int NATIVE_FILE_DIALOG_FIRST_FILTER_INDEX = 1;
inline constexpr std::string_view CHAT_SFX_YES_TEXT = "1";
inline constexpr std::string_view CHAT_SFX_NO_TEXT = "2";

inline constexpr std::string_view MAIN_MENU_TITLE_TEXT = "Age of Affinities";
inline constexpr int MAIN_MENU_TITLE_PIXEL_SCALE = 3;
inline constexpr int MAIN_MENU_PANEL_WIDTH_PX = 340;
inline constexpr int MAIN_MENU_PANEL_MARGIN_PX = 36;
inline constexpr int MAIN_MENU_PANEL_PADDING_PX = 14;
inline constexpr int MAIN_MENU_BUTTON_HEIGHT_PX = 36;
inline constexpr int MAIN_MENU_BUTTON_GAP_PX = 8;
inline constexpr int MAIN_MENU_SPLIT_GAP_PX = 10;
inline constexpr int MAIN_MENU_SPLIT_THICKNESS_PX = 2;
inline constexpr int MAIN_MENU_ROW_HEIGHT_PX = 30;
inline constexpr int MAIN_MENU_LABEL_GAP_PX = 20;
inline constexpr int MAIN_MENU_DIALOG_WIDTH_PX = 470;
inline constexpr int MAIN_MENU_LOBBY_WIDTH_PX = 740;
inline constexpr int MAIN_MENU_LOBBY_ROW_HEIGHT_PX = 26;
inline constexpr int MAIN_MENU_LOBBY_PING_WIDTH_PX = 70;
inline constexpr int MAIN_MENU_LOBBY_READY_WIDTH_PX = 120;
inline constexpr int MAIN_MENU_WIDE_BUTTON_WIDTH_PX = 130;
inline constexpr int MENU_PATTERN_PREVIEW_SIZE_PX = 260;
inline constexpr int MENU_PATTERN_PREVIEW_GAP_PX = 16;
inline constexpr int MENU_PATTERN_PREVIEW_PAD_PX = 12;
inline constexpr int MENU_PATTERN_PREVIEW_SAMPLE_EDGE = 16;
inline constexpr std::uint64_t MENU_PATTERN_PREVIEW_SEED = 0xA0A5C0DEULL;
inline constexpr float MENU_PATTERN_PREVIEW_GRASS_R = 0.27F;
inline constexpr float MENU_PATTERN_PREVIEW_GRASS_G = 0.43F;
inline constexpr float MENU_PATTERN_PREVIEW_GRASS_B = 0.20F;
inline constexpr float MENU_PATTERN_PREVIEW_SNOW_R = 0.78F;
inline constexpr float MENU_PATTERN_PREVIEW_SNOW_G = 0.82F;
inline constexpr float MENU_PATTERN_PREVIEW_SNOW_B = 0.86F;
inline constexpr float MENU_PATTERN_PREVIEW_SAND_R = 0.76F;
inline constexpr float MENU_PATTERN_PREVIEW_SAND_G = 0.70F;
inline constexpr float MENU_PATTERN_PREVIEW_SAND_B = 0.50F;
inline constexpr float MENU_PATTERN_PREVIEW_FOREST_R = 0.08F;
inline constexpr float MENU_PATTERN_PREVIEW_FOREST_G = 0.31F;
inline constexpr float MENU_PATTERN_PREVIEW_FOREST_B = 0.12F;
inline constexpr float MENU_PATTERN_PREVIEW_GOLD_R = 0.86F;
inline constexpr float MENU_PATTERN_PREVIEW_GOLD_G = 0.71F;
inline constexpr float MENU_PATTERN_PREVIEW_GOLD_B = 0.16F;
inline constexpr float MENU_PATTERN_PREVIEW_BERRIES_R = 0.63F;
inline constexpr float MENU_PATTERN_PREVIEW_BERRIES_G = 0.16F;
inline constexpr float MENU_PATTERN_PREVIEW_BERRIES_B = 0.27F;
inline constexpr float MENU_PATTERN_PREVIEW_LAKE_R = 0.20F;
inline constexpr float MENU_PATTERN_PREVIEW_LAKE_G = 0.43F;
inline constexpr float MENU_PATTERN_PREVIEW_LAKE_B = 0.82F;
inline constexpr float MENU_PATTERN_PREVIEW_START_R = 1.00F;
inline constexpr float MENU_PATTERN_PREVIEW_START_G = 0.90F;
inline constexpr float MENU_PATTERN_PREVIEW_START_B = 0.31F;
inline constexpr float MENU_PATTERN_PREVIEW_DIAMOND_EDGE_R = 0.31F;
inline constexpr float MENU_PATTERN_PREVIEW_DIAMOND_EDGE_G = 0.35F;
inline constexpr float MENU_PATTERN_PREVIEW_DIAMOND_EDGE_B = 0.27F;
inline constexpr int MAIN_MENU_MAX_NAME_LENGTH = 16;
inline constexpr int MAIN_MENU_MAX_ADDRESS_LENGTH = 32;
inline constexpr int MAIN_MENU_MAX_PORT_LENGTH = 5;
inline constexpr char32_t MAIN_MENU_MIN_PRINTABLE_CHAR = 32U;
inline constexpr char32_t MAIN_MENU_MAX_PRINTABLE_CHAR = 126U;
inline constexpr float MAIN_MENU_SPLIT_LINE_R = 0.55F;
inline constexpr float MAIN_MENU_SPLIT_LINE_G = 0.55F;
inline constexpr float MAIN_MENU_SPLIT_LINE_B = 0.55F;
inline constexpr float MAIN_MENU_FIELD_BG_R = 0.10F;
inline constexpr float MAIN_MENU_FIELD_BG_G = 0.11F;
inline constexpr float MAIN_MENU_FIELD_BG_B = 0.13F;
inline constexpr float MAIN_MENU_FIELD_FOCUS_R = 0.30F;
inline constexpr float MAIN_MENU_FIELD_FOCUS_G = 0.45F;
inline constexpr float MAIN_MENU_FIELD_FOCUS_B = 0.30F;
inline constexpr float MAIN_MENU_PANEL_ALPHA = 0.92F;
inline constexpr float MAIN_MENU_STATUS_R = 0.85F;
inline constexpr float MAIN_MENU_STATUS_G = 0.70F;
inline constexpr float MAIN_MENU_STATUS_B = 0.35F;
inline constexpr float MAIN_MENU_READY_R = 0.35F;
inline constexpr float MAIN_MENU_READY_G = 0.85F;
inline constexpr float MAIN_MENU_READY_B = 0.35F;

inline constexpr float MENU_SLIDESHOW_DWELL_SECONDS = 15.0F;
inline constexpr float MENU_SLIDESHOW_FADE_SECONDS = 4.0F;
inline constexpr std::array<std::string_view, 7> MENU_SLIDESHOW_RELATIVE_PATHS = {
    "textures/Tree bg pack/Tree Background2.png",
    "textures/Tree bg pack/Tree Background3.png",
    "textures/Sandstorm/Sandstorm1.png",
    "textures/Sandstorm/Sandstorm3.png",
    "textures/Sandstorm/Sandstorm5.png",
    "textures/Enchanted River Forest/Enchanted River Forest2.png",
    "textures/Enchanted River Forest/Enchanted River Forest5.png",
};

inline constexpr std::string_view MULTIPLAYER_DEFAULT_PLAYER_NAME = "Player";
inline constexpr std::string_view MULTIPLAYER_DEFAULT_JOIN_ADDRESS = "127.0.0.1";
inline constexpr std::uint8_t MULTIPLAYER_MIN_PLAYER_COUNT = 2U;
inline constexpr std::uint8_t MULTIPLAYER_MAX_PLAYER_COUNT = 8U;
inline constexpr std::string_view MULTIPLAYER_BROWSE_GAMES_LABEL = "Browse games";
inline constexpr std::string_view MULTIPLAYER_LAN_TCP_IP_LABEL = "LAN TCP/IP";
inline constexpr std::string_view MULTIPLAYER_BROWSE_TITLE = "Browse Games";
inline constexpr std::string_view MULTIPLAYER_FILTER_ALL_LABEL = "Filter: All";
inline constexpr std::string_view MULTIPLAYER_FILTER_PUBLIC_LABEL = "Filter: Public servers";
inline constexpr std::string_view MULTIPLAYER_FILTER_LAN_LABEL = "Filter: LAN TCP/IP";
inline constexpr std::string_view MULTIPLAYER_REFRESH_LABEL = "Refresh";
inline constexpr int MULTIPLAYER_BROWSE_LIST_ROWS = 6;
inline constexpr std::string_view MULTIPLAYER_NO_GAMES_LABEL = "No games found";
inline constexpr std::string_view MULTIPLAYER_PUBLIC_FILTER_BLOCKED_LABEL =
    "Public servers are not available yet";
inline constexpr std::string_view MULTIPLAYER_NAME_REQUIRED_LABEL = "Enter a player name";
inline constexpr std::string_view MULTIPLAYER_SELECT_GAME_LABEL = "Select a game first";
inline constexpr std::string_view MULTIPLAYER_LOBBY_FULL_LABEL = "Lobby is full";
// Host rebinds the match port after the lobby transport is torn down; peers wait it out.
inline constexpr int MULTIPLAYER_MATCH_START_FLUSH_MS = 400;
inline constexpr int MULTIPLAYER_MATCH_CONNECT_DELAY_MS = 900;
// ENet hands out the lowest free client slot, so peers connect in player-slot order
// to make the match transport slot match the slot they were given in the lobby.
inline constexpr int MULTIPLAYER_MATCH_CONNECT_SLOT_STAGGER_MS = 500;
inline constexpr int MULTIPLAYER_LOBBY_CONNECT_TIMEOUT_MS = 8000;
// After LobbyJoin is sent, fail if the host never answers with LobbyState (e.g. mid-match).
inline constexpr int MULTIPLAYER_LOBBY_JOIN_RESPONSE_TIMEOUT_MS = 8000;

inline constexpr int PATHFIND_COST_SCALE = 10;
inline constexpr int PATHFIND_CARDINAL_STEP_COST = 10;
inline constexpr int PATHFIND_DIAGONAL_STEP_COST = 14;
inline constexpr int PATHFIND_KNIGHT_STEP_COST = 22;
inline constexpr float PATHFIND_LINE_T_EPSILON = 0.0001F;
inline constexpr int PATHFIND_STEP_CELLS_MAX = 16;
inline constexpr int PATHFIND_MAX_EXPANDED_NODES = 2048;
inline constexpr int MOVE_REPATH_SEGMENT_LOOKAHEAD_TICKS = 1;
inline constexpr float MOVE_PATH_STEP_REACHED_TILE_DISTANCE = 0.08F;
inline constexpr float MOVE_GOAL_TILE_EDGE_INSET = 0.08F;
inline constexpr int MOVE_UNWALKABLE_GOAL_SEARCH_RADIUS = 3;
inline constexpr int ATTACK_PATH_DIRECT_LINE_MAX_STEPS = 512;
/// Euclidean tile distance from unit world pos to resource/building AABB for gather/deposit.
inline constexpr float WORK_INTERACT_RANGE_TILES = 0.8F;
inline constexpr int MOVE_FORMATION_GOAL_MAX_RING = 8;

} // namespace aoa::constants
