#pragma once

#include <cstdint>
#include <string_view>

#include <array>

namespace aoa::constants {

inline constexpr std::string_view WINDOW_TITLE = "Age of Affinities";
inline constexpr std::uint32_t DEFAULT_WINDOW_WIDTH = 1280U;
inline constexpr std::uint32_t DEFAULT_WINDOW_HEIGHT = 720U;

inline constexpr int SIM_TICKS_PER_SECOND = 20;
inline constexpr int TOWN_CENTER_FOOTPRINT_TILES = 3;
inline constexpr int HOUSE_FOOTPRINT_TILES = 2;
inline constexpr int WORKER_GATHER_INTERVAL_TICKS = 35;
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
inline constexpr int CAMERA_MAP_BOUNDS_PADDING_TILES = 4;

inline constexpr int HUD_ICON_TILE_SIZE_PX = 32;
inline constexpr int HUD_ICON_DRAW_SIZE_PX = 48;
inline constexpr int HUD_ICON_OPTION_INSET_PX = 4;
inline constexpr int HUD_ICON_TEXT_GAP_PX = 4;
inline constexpr int HUD_OPTIONS_BUTTON_PRESS_OFFSET_PX = 2;
inline constexpr float HUD_OPTIONS_BUTTON_HOVER_BRIGHTEN = 1.22F;
inline constexpr float HUD_OPTIONS_BUTTON_PRESS_DARKEN = 0.78F;
inline constexpr int MOVE_BLOCKED_REPATH_COOLDOWN_TICKS = 5;
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
inline constexpr float RENDER_TOWN_CENTER_SPRITE_WIDTH_SCALE = 1.18F;
inline constexpr float RENDER_GOLD_MINE_SPRITE_WIDTH_SCALE = 0.72F;
// Sprite draw offsets in grid tiles (+X = SE, +Y = SW on the iso diamond compass).
inline constexpr float RENDER_GOLD_MINE_SPRITE_OFFSET_X = 0.5F;
inline constexpr float RENDER_GOLD_MINE_SPRITE_OFFSET_Y = 0.5F;
inline constexpr float RENDER_TOWN_CENTER_SPRITE_OFFSET_X = 3.0F;
inline constexpr float RENDER_TOWN_CENTER_SPRITE_OFFSET_Y = 3.0F;
inline constexpr float RENDER_HOUSE_SPRITE_OFFSET_X = 0.8F;
inline constexpr float RENDER_HOUSE_SPRITE_OFFSET_Y = 0.8F;
inline constexpr float RENDER_BERRY_SPRITE_OFFSET_X = 0.5F;
inline constexpr float RENDER_BERRY_SPRITE_OFFSET_Y = 0.5F;
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
inline constexpr float RENDER_GRASS_UV_LEFT = 0.08F;
inline constexpr float RENDER_GRASS_UV_TOP = 0.02F;
inline constexpr float RENDER_GRASS_UV_RIGHT = 0.92F;
inline constexpr float RENDER_GRASS_UV_BOTTOM = 0.44F;

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
inline constexpr int CIVIL_POPULATION_MAP_CAP_OPTION_B = 20;
inline constexpr int CIVIL_POPULATION_MAP_CAP_OPTION_C = 35;
inline constexpr int TOWN_CENTER_MANA_GEN_AMOUNT = 1;
inline constexpr int TOWN_CENTER_MANA_GEN_INTERVAL_TICKS = SIM_TICKS_PER_SECOND * 5;
inline constexpr int TOWN_CENTER_BUILD_WOOD_COST = 250;
inline constexpr int HOUSE_BUILD_WOOD_COST = 30;
inline constexpr int BERRY_BUSH_FOOD_CAPACITY = 75;
inline constexpr int BERRY_PATCH_TILE_COUNT = 7;
inline constexpr int BLUEBERRY_CLUSTER_SIZE = 3;
inline constexpr int BLUEBERRY_CLUSTER_COUNT = 3;
inline constexpr int BLUEBERRY_MIN_DISTANCE_FROM_BASE = 18;
inline constexpr int BLUEBERRY_PLACEMENT_MAX_ATTEMPTS = 512;
inline constexpr int GOLD_MINE_MONEY_CAPACITY = 450;
inline constexpr int GOLD_MINE_NEAR_BASE_COUNT = 3;
inline constexpr int GOLD_MINE_VARIANT_COUNT = 4;
inline constexpr int WORKER_FOOD_COST = 45;
inline constexpr int MILITIA_FOOD_COST = 65;
inline constexpr int HUD_COMMAND_PANEL_KEY_PRESS_TTL_MS = 120;
// Construction: 36 HP per hit at ~1.34 hits/s (20 TPS => 15-tick interval).
inline constexpr int WORKER_BUILD_HP_PER_HIT = 36;
inline constexpr float WORKER_BUILD_HITS_PER_SECOND = 1.34F;
inline constexpr int WORKER_BUILD_HIT_INTERVAL_TICKS = 15;
inline constexpr int DEFAULT_STARTING_STOCKPILE_FOOD = 100;
inline constexpr int DEFAULT_STARTING_STOCKPILE_WOOD = 200;
inline constexpr int DEFAULT_STARTING_STOCKPILE_MONEY = 100;
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
inline constexpr int CONSTRUCTION_VISION_FOOTPRINT_PADDING_TILES = 1;
inline constexpr int CHAT_MAX_MESSAGE_LENGTH = 80;
inline constexpr int CHAT_INPUT_VISIBLE_CHARS = 80;
inline constexpr int CHAT_MAX_VISIBLE_LINES = 8;
inline constexpr int CHAT_MESSAGE_TTL_MS = 10000;
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
inline constexpr float AUDIO_SFX_VOLUME = 80.0F;
inline constexpr float AUDIO_MUSIC_VOLUME = 45.0F;
inline constexpr float AUDIO_MASTER_VOLUME = 100.0F;
inline constexpr float AUDIO_VOLUME_MIN = 0.0F;
inline constexpr float AUDIO_VOLUME_MAX = 100.0F;
inline constexpr int AUDIO_MAX_CONCURRENT_SFX = 16;

inline constexpr float HUD_MENU_BUTTON_WIDTH_FRACTION = 0.08F;
inline constexpr float HUD_MENU_BUTTON_HEIGHT_FRACTION = 0.045F;
inline constexpr int HUD_MENU_BUTTON_MARGIN_PX = 10;
inline constexpr float HUD_GAME_MENU_WIDTH_FRACTION = 0.28F;
inline constexpr float HUD_GAME_MENU_HEIGHT_FRACTION = 0.52F;
inline constexpr float HUD_SETTINGS_WIDTH_FRACTION = 0.36F;
inline constexpr float HUD_SETTINGS_HEIGHT_FRACTION = 0.48F;
inline constexpr int HUD_GAME_MENU_BUTTON_COUNT = 6;
inline constexpr int HUD_SETTINGS_TAB_COUNT = 2;
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
inline constexpr int MAIN_MENU_LOBBY_WIDTH_PX = 620;
inline constexpr int MAIN_MENU_LOBBY_ROW_HEIGHT_PX = 26;
inline constexpr int MAIN_MENU_WIDE_BUTTON_WIDTH_PX = 130;
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
inline constexpr std::uint8_t MULTIPLAYER_MAX_PLAYER_COUNT = 4U;
// Host rebinds the match port after the lobby transport is torn down; peers wait it out.
inline constexpr int MULTIPLAYER_MATCH_START_FLUSH_MS = 400;
inline constexpr int MULTIPLAYER_MATCH_CONNECT_DELAY_MS = 900;
// ENet hands out the lowest free client slot, so peers connect in player-slot order
// to make the match transport slot match the slot they were given in the lobby.
inline constexpr int MULTIPLAYER_MATCH_CONNECT_SLOT_STAGGER_MS = 500;
inline constexpr int MULTIPLAYER_LOBBY_CONNECT_TIMEOUT_MS = 8000;

inline constexpr int PATHFIND_COST_SCALE = 10;
inline constexpr int PATHFIND_CARDINAL_STEP_COST = 10;
inline constexpr int PATHFIND_DIAGONAL_STEP_COST = 14;
inline constexpr int PATHFIND_KNIGHT_STEP_COST = 22;
inline constexpr int MOVE_REPATH_SEGMENT_LOOKAHEAD_TICKS = 1;
inline constexpr float MOVE_PATH_STEP_REACHED_TILE_DISTANCE = 0.08F;
inline constexpr float MOVE_GOAL_TILE_EDGE_INSET = 0.08F;
inline constexpr int ATTACK_PATH_DIRECT_LINE_MAX_STEPS = 512;

} // namespace aoa::constants
