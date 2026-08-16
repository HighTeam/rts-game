#include "render/game_renderer.hpp"

#include "app/command_panel.hpp"
#include "app/game_menu.hpp"
#include "core/constants.hpp"
#include "core/grid.hpp"
#include "core/runtime_paths.hpp"
#include "render/sim_render_snapshot.hpp"
#include "render/camera_settings.hpp"
#include "sim/components/grid_position.hpp"
#include "sim/components/health.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/map_grid.hpp"
#include "sim/components/building_footprint.hpp"
#include "sim/systems/visibility_system.hpp"
#include "sim/components/player_slot.hpp"
#include "sim/components/tags.hpp"
#include "sim/components/world_position.hpp"
#include "sim/components/movement.hpp"
#include "sim/spawn/unit_spawn.hpp"

#include "math/fixed.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <SFML/Window/Context.hpp>

#include <glad/glad.h>

#include <array>
#include <stdexcept>
#include <string>

namespace aoa::render {

namespace {

constexpr const char* SCENE_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
out vec3 fragment_color;
void main()
{
    gl_Position = vec4(position, 1.0);
    fragment_color = color;
}
)";

constexpr const char* SCENE_FRAGMENT_SHADER = R"(
#version 330 core
in vec3 fragment_color;
out vec4 output_color;
void main()
{
    output_color = vec4(fragment_color, 1.0);
}
)";

constexpr const char* TEXTURED_SCENE_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 tint;
out vec2 fragment_uv;
out vec4 fragment_tint;
void main()
{
    gl_Position = vec4(position, 1.0);
    fragment_uv = uv;
    fragment_tint = tint;
}
)";

constexpr const char* TEXTURED_SCENE_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 fragment_uv;
in vec4 fragment_tint;
uniform sampler2D scene_texture;
out vec4 output_color;
void main()
{
    vec4 sampled = texture(scene_texture, fragment_uv);
    if (sampled.a < 0.01) {
        discard;
    }
    if (sampled.a < 0.55 && max(max(sampled.r, sampled.g), sampled.b) < 0.12) {
        discard;
    }
    output_color = sampled * fragment_tint;
}
)";

unsigned int compile_shader(const unsigned int type, const char* source)
{
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        std::array<char, 512> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(std::string("Shader compile failed: ") + log.data());
    }

    return shader;
}

unsigned int link_program(const unsigned int vertex_shader, const unsigned int fragment_shader)
{
    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        std::array<char, 512> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(std::string("Shader link failed: ") + log.data());
    }

    return program;
}

[[nodiscard]] bool fog_data_is_player_slice(
    const std::vector<std::uint8_t>& fog,
    const int map_width,
    const int map_height)
{
    if (map_width <= 0 || map_height <= 0 || fog.empty()) {
        return false;
    }

    return fog.size() == static_cast<std::size_t>(map_width * map_height);
}

[[nodiscard]] std::size_t fog_tile_index(
    const std::vector<std::uint8_t>& fog,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_data_is_player_slice(fog, map_width, map_height)) {
        return static_cast<std::size_t>(y * map_width + x);
    }

    return static_cast<std::size_t>(player_slot * map_width * map_height + y * map_width + x);
}

[[nodiscard]] bool is_fog_tile_unexplored(
    const std::vector<std::uint8_t>& fog_visible,
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_visible.empty() || fog_explored.empty() || map_width <= 0 || map_height <= 0) {
        return false;
    }

    const std::size_t index =
        fog_tile_index(fog_visible, map_width, map_height, player_slot, x, y);
    if (index >= fog_visible.size() || index >= fog_explored.size()) {
        return false;
    }

    return fog_visible[index] == 0U && fog_explored[index] == 0U;
}

float fog_tile_brightness(
    const std::vector<std::uint8_t>& fog_visible,
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_visible.empty() || fog_explored.empty() || map_width <= 0 || map_height <= 0) {
        return 1.0F;
    }

    const std::size_t index =
        fog_tile_index(fog_visible, map_width, map_height, player_slot, x, y);
    if (index >= fog_visible.size() || index >= fog_explored.size()) {
        return 1.0F;
    }

    if (fog_visible[index] != 0U) {
        return 1.0F;
    }

    if (fog_explored[index] != 0U) {
        return aoa::constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
    }

    return aoa::constants::FOG_UNEXPLORED_BRIGHTNESS;
}

[[nodiscard]] bool fog_tile_is_in_shroud(
    const std::vector<std::uint8_t>& fog_visible,
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const int x,
    const int y)
{
    if (fog_visible.empty() || fog_explored.empty()) {
        return false;
    }

    if (is_fog_tile_unexplored(fog_visible, fog_explored, map_width, map_height, player_slot, x, y)) {
        return false;
    }

    return fog_tile_brightness(fog_visible, fog_explored, map_width, map_height, player_slot, x, y)
        < 1.0F;
}

struct FogTileCornerBrightness {
    float top{1.0F};
    float right{1.0F};
    float bottom{1.0F};
    float left{1.0F};
};

// Memory-only brightness: whether a tile has ever been explored, ignoring
// whether it is *currently* visible. This deliberately excludes the "fully
// visible" (1.0) state so blurring it can never bleed live-vision brightness
// into tiles the player hasn't explored yet - it only softens the boundary
// between remembered (shroud) and never-seen ground, which is static and
// safe to blur. The live vision edge is instead handled exactly by
// `vision_strength_at`, which cannot exceed a unit's real vision radius.
[[nodiscard]] std::vector<float> build_fog_memory_scalar_field(
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot)
{
    std::vector<float> field(static_cast<std::size_t>(map_width * map_height), 0.0F);
    for (int y = 0; y < map_height; ++y) {
        for (int x = 0; x < map_width; ++x) {
            const std::size_t index =
                fog_tile_index(fog_explored, map_width, map_height, player_slot, x, y);
            const bool explored = index < fog_explored.size() && fog_explored[index] != 0U;
            field[static_cast<std::size_t>(y * map_width + x)] =
                explored ? constants::FOG_EXPLORED_SHROUD_BRIGHTNESS : constants::FOG_UNEXPLORED_BRIGHTNESS;
        }
    }

    return field;
}

// Smooth, distance-accurate reveal strength in [0, 1] for a world point,
// computed directly from real vision sources rather than blurring a
// discretized grid. It is exactly 0 at and beyond a source's true vision
// radius (so it can never preview unexplored content) and ramps up to 1
// over `fade_width` tiles as the point gets closer to the source. Because it
// reads each source's own radius, it automatically matches whatever vision
// range a given unit or building type has.
[[nodiscard]] float vision_strength_at(
    const std::vector<sim::systems::VisionSource>& vision_sources,
    const float world_x,
    const float world_y,
    const float fade_width)
{
    float strength = 0.0F;
    for (const sim::systems::VisionSource& source : vision_sources) {
        const float dx = world_x - source.origin_x;
        const float dy = world_y - source.origin_y;
        const float distance = std::sqrt((dx * dx) + (dy * dy));
        const float value = std::clamp((source.radius - distance) / fade_width, 0.0F, 1.0F);
        strength = std::max(strength, value);
    }

    return strength;
}

// Network snapshots don't carry each unit's exact vision_range (only pose
// data), so approximate it from the same category flags/defaults the sim
// uses. Good enough for the cosmetic reveal fade in spectator/replay views.
[[nodiscard]] int approximate_vision_range_for_pose(const RenderEntityPose& pose)
{
    if (pose.under_construction) {
        if (pose.health_current <= constants::CONSTRUCTION_VISION_ACTIVE_MIN_HP) {
            return 0;
        }

        // Exact footprint only — cosmetic snapshot fade uses per-tile sources instead.
        return 0;
    }

    if (pose.is_town_center) {
        return constants::DEFAULT_TOWN_CENTER_VISION_RANGE;
    }

    if (pose.is_worker) {
        return constants::DEFAULT_WORKER_VISION_RANGE;
    }

    return constants::DEFAULT_UNIT_VISION_RANGE;
}

// Wall-clock driven so the lake keeps shimmering while the sim is paused.
[[nodiscard]] int mana_lake_animation_frame()
{
    int cycle_ms = 0;
    for (const int duration_ms : constants::MANA_LAKE_ANIMATION_FRAME_DURATIONS_MS) {
        cycle_ms += duration_ms;
    }

    if (cycle_ms <= 0) {
        return 0;
    }

    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    int position_ms = static_cast<int>(elapsed_ms % static_cast<long long>(cycle_ms));
    for (int frame = 0; frame < constants::MANA_LAKE_ANIMATION_FRAME_COUNT; ++frame) {
        position_ms -= constants::MANA_LAKE_ANIMATION_FRAME_DURATIONS_MS[static_cast<std::size_t>(frame)];
        if (position_ms < 0) {
            return frame;
        }
    }

    return constants::MANA_LAKE_ANIMATION_FRAME_COUNT - 1;
}

[[nodiscard]] SceneTextureKind textured_building_kind(
    const bool is_house,
    const bool is_lumberjack,
    const bool is_extractor,
    const bool is_enemy)
{
    if (is_extractor) {
        return is_enemy ? SceneTextureKind::ExtractorEnemy : SceneTextureKind::ExtractorFriendly;
    }

    if (is_lumberjack) {
        return is_enemy ? SceneTextureKind::LumberjackEnemy : SceneTextureKind::LumberjackFriendly;
    }

    if (is_house) {
        return is_enemy ? SceneTextureKind::HouseEnemy : SceneTextureKind::HouseFriendly;
    }

    return is_enemy ? SceneTextureKind::TownCenterEnemy : SceneTextureKind::TownCenterFriendly;
}

[[nodiscard]] float textured_building_offset_x(
    const bool is_house,
    const bool is_lumberjack,
    const bool is_extractor)
{
    if (is_extractor) {
        return constants::RENDER_EXTRACTOR_SPRITE_OFFSET_X;
    }

    if (is_lumberjack) {
        return constants::RENDER_LUMBERJACK_SPRITE_OFFSET_X;
    }

    if (is_house) {
        return constants::RENDER_HOUSE_SPRITE_OFFSET_X;
    }

    return constants::RENDER_TOWN_CENTER_SPRITE_OFFSET_X;
}

[[nodiscard]] float textured_building_offset_y(
    const bool is_house,
    const bool is_lumberjack,
    const bool is_extractor)
{
    if (is_extractor) {
        return constants::RENDER_EXTRACTOR_SPRITE_OFFSET_Y;
    }

    if (is_lumberjack) {
        return constants::RENDER_LUMBERJACK_SPRITE_OFFSET_Y;
    }

    if (is_house) {
        return constants::RENDER_HOUSE_SPRITE_OFFSET_Y;
    }

    return constants::RENDER_TOWN_CENTER_SPRITE_OFFSET_Y;
}

[[nodiscard]] bool is_textured_building_kind(const SceneTextureKind kind)
{
    return kind == SceneTextureKind::TownCenterFriendly
        || kind == SceneTextureKind::TownCenterEnemy
        || kind == SceneTextureKind::HouseFriendly
        || kind == SceneTextureKind::HouseEnemy
        || kind == SceneTextureKind::LumberjackFriendly
        || kind == SceneTextureKind::LumberjackEnemy
        || kind == SceneTextureKind::ExtractorFriendly
        || kind == SceneTextureKind::ExtractorEnemy;
}

[[nodiscard]] int placement_ghost_footprint_tiles(const app::CommandPanelMode mode)
{
    if (mode == app::CommandPanelMode::PlaceHouse) {
        return constants::HOUSE_FOOTPRINT_TILES;
    }

    if (mode == app::CommandPanelMode::PlaceLumberjack) {
        return constants::LUMBERJACK_FOOTPRINT_TILES;
    }

    if (mode == app::CommandPanelMode::PlaceExtractor) {
        return constants::EXTRACTOR_FOOTPRINT_TILES;
    }

    return constants::TOWN_CENTER_FOOTPRINT_TILES;
}

[[nodiscard]] bool placement_ghost_footprint_on_map(
    const int map_width,
    const int map_height,
    const core::GridPos anchor,
    const int footprint)
{
    if (map_width <= 0 || map_height <= 0) {
        return false;
    }

    for (int y = 0; y < footprint; ++y) {
        for (int x = 0; x < footprint; ++x) {
            if (!core::is_inside_grid({anchor.x + x, anchor.y + y}, map_width, map_height)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] std::vector<sim::systems::VisionSource> collect_vision_sources_from_snapshot(
    const SimRenderSnapshot& snapshot,
    const std::uint8_t player_slot)
{
    std::vector<sim::systems::VisionSource> sources{};

    const auto collect_from = [&](const std::vector<RenderEntityPose>& poses) {
        for (const RenderEntityPose& pose : poses) {
            if (pose.player_slot != player_slot || pose.health_current <= 0) {
                continue;
            }

            if (pose.under_construction) {
                if (pose.health_current <= constants::CONSTRUCTION_VISION_ACTIVE_MIN_HP) {
                    continue;
                }

                for (int y = 0; y < pose.footprint_height; ++y) {
                    for (int x = 0; x < pose.footprint_width; ++x) {
                        sources.push_back(sim::systems::VisionSource{
                            static_cast<float>(pose.grid_x + x) + 0.5F,
                            static_cast<float>(pose.grid_y + y) + 0.5F,
                            constants::CONSTRUCTION_VISION_PER_TILE_RADIUS,
                        });
                    }
                }
                continue;
            }

            const float radius = static_cast<float>(approximate_vision_range_for_pose(pose))
                + constants::FOG_VISION_RADIUS_TILE_PADDING;
            sources.push_back(sim::systems::VisionSource{pose.cur_x, pose.cur_y, radius});
        }
    };

    collect_from(snapshot.units);
    collect_from(snapshot.buildings);

    return sources;
}

[[nodiscard]] std::vector<float> gaussian_blur_field(
    const std::vector<float>& field,
    const int width,
    const int height,
    const int radius,
    const float sigma)
{
    std::vector<float> kernel(static_cast<std::size_t>(radius * 2 + 1));
    float kernel_sum = 0.0F;
    for (int i = -radius; i <= radius; ++i) {
        const float weight =
            std::exp(-(static_cast<float>(i * i)) / (2.0F * sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = weight;
        kernel_sum += weight;
    }

    for (float& weight : kernel) {
        weight /= kernel_sum;
    }

    const auto sample_clamped = [&](const std::vector<float>& source, int x, int y) {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return source[static_cast<std::size_t>(y * width + x)];
    };

    std::vector<float> horizontal(field.size(), 0.0F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0F;
            for (int i = -radius; i <= radius; ++i) {
                sum += sample_clamped(field, x + i, y)
                    * kernel[static_cast<std::size_t>(i + radius)];
            }

            horizontal[static_cast<std::size_t>(y * width + x)] = sum;
        }
    }

    std::vector<float> vertical(field.size(), 0.0F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0F;
            for (int i = -radius; i <= radius; ++i) {
                sum += sample_clamped(horizontal, x, y + i)
                    * kernel[static_cast<std::size_t>(i + radius)];
            }

            vertical[static_cast<std::size_t>(y * width + x)] = sum;
        }
    }

    return vertical;
}

[[nodiscard]] float sample_blurred_field_bilinear(
    const std::vector<float>& field,
    const int width,
    const int height,
    const float sample_x,
    const float sample_y)
{
    const float fx = std::clamp(sample_x, 0.0F, static_cast<float>(width - 1));
    const float fy = std::clamp(sample_y, 0.0F, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    const float v00 = field[static_cast<std::size_t>(y0 * width + x0)];
    const float v10 = field[static_cast<std::size_t>(y0 * width + x1)];
    const float v01 = field[static_cast<std::size_t>(y1 * width + x0)];
    const float v11 = field[static_cast<std::size_t>(y1 * width + x1)];

    const float top = v00 + (v10 - v00) * tx;
    const float bottom = v01 + (v11 - v01) * tx;
    return top + (bottom - top) * ty;
}

[[nodiscard]] std::vector<float> build_fog_vertex_brightness(
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const std::vector<sim::systems::VisionSource>& vision_sources)
{
    if (map_width <= 0 || map_height <= 0) {
        return {};
    }

    const std::vector<float> memory_field = build_fog_memory_scalar_field(
        fog_explored,
        map_width,
        map_height,
        player_slot);

    const std::vector<float> blurred_memory_field = gaussian_blur_field(
        memory_field,
        map_width,
        map_height,
        constants::FOG_BLUR_RADIUS_TILES,
        constants::FOG_BLUR_SIGMA);

    const int vertex_width = map_width + 1;
    const int vertex_height = map_height + 1;
    std::vector<float> vertex_field(
        static_cast<std::size_t>(vertex_width * vertex_height),
        0.0F);

    for (int vertex_y = 0; vertex_y < vertex_height; ++vertex_y) {
        for (int vertex_x = 0; vertex_x < vertex_width; ++vertex_x) {
            const float memory_brightness = sample_blurred_field_bilinear(
                blurred_memory_field,
                map_width,
                map_height,
                static_cast<float>(vertex_x) - 0.5F,
                static_cast<float>(vertex_y) - 0.5F);
            const float live_strength = vision_strength_at(
                vision_sources,
                static_cast<float>(vertex_x),
                static_cast<float>(vertex_y),
                constants::FOG_LIVE_EDGE_FADE_WIDTH_TILES);
            vertex_field[static_cast<std::size_t>(vertex_y * vertex_width + vertex_x)] =
                std::max(memory_brightness, live_strength);
        }
    }

    return vertex_field;
}

[[nodiscard]] FogTileCornerBrightness fog_tile_corner_brightness_from_vertices(
    const std::vector<float>& fog_vertex_brightness,
    const int map_width,
    const int map_height,
    const int grid_x,
    const int grid_y)
{
    const int vertex_width = map_width + 1;
    const auto sample_vertex = [&](const int vertex_x, const int vertex_y) {
        if (vertex_x < 0 || vertex_y < 0 || vertex_x >= vertex_width || vertex_y >= map_height + 1) {
            return constants::FOG_UNEXPLORED_BRIGHTNESS;
        }

        return fog_vertex_brightness[static_cast<std::size_t>(vertex_y * vertex_width + vertex_x)];
    };

    return FogTileCornerBrightness{
        sample_vertex(grid_x, grid_y),
        sample_vertex(grid_x + 1, grid_y),
        sample_vertex(grid_x + 1, grid_y + 1),
        sample_vertex(grid_x, grid_y + 1),
    };
}

// Objects sitting on a tile (trees, decorations) must fade in step with the
// ground beneath them. Sampling the same blurred vertex field the ground
// uses (instead of the tile's hard fog brightness) keeps an object from
// looking brighter than the darkened ground around it near the vision edge.
[[nodiscard]] float fog_object_brightness(
    const std::vector<float>* fog_vertex_brightness,
    const int map_width,
    const int map_height,
    const int grid_x,
    const int grid_y,
    const float fallback_brightness)
{
    if (fog_vertex_brightness == nullptr || map_width <= 0 || map_height <= 0) {
        return fallback_brightness;
    }

    const FogTileCornerBrightness corners = fog_tile_corner_brightness_from_vertices(
        *fog_vertex_brightness,
        map_width,
        map_height,
        grid_x,
        grid_y);
    return (corners.top + corners.right + corners.bottom + corners.left) * 0.25F;
}

[[nodiscard]] bool cell_covered_by_building_footprint(
    const int grid_x,
    const int grid_y,
    const std::vector<core::GridPos>& building_anchors,
    const std::vector<sim::components::BuildingFootprint>& building_footprints)
{
    for (std::size_t index = 0U; index < building_anchors.size(); ++index) {
        const sim::components::GridPosition anchor{building_anchors[index]};
        const sim::components::BuildingFootprint& footprint = building_footprints[index];
        if (sim::components::building_contains_cell(anchor, footprint, core::GridPos{grid_x, grid_y})) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] float town_center_sprite_width_scale(
    const sim::components::BuildingFootprint& footprint)
{
    return static_cast<float>(std::max(footprint.width, footprint.height))
        * constants::RENDER_TOWN_CENTER_SPRITE_WIDTH_SCALE;
}

[[nodiscard]] float extractor_sprite_width_scale(
    const sim::components::BuildingFootprint& footprint)
{
    return static_cast<float>(std::max(footprint.width, footprint.height))
        * constants::RENDER_EXTRACTOR_SPRITE_WIDTH_SCALE;
}

[[nodiscard]] float textured_building_width_scale(
    const bool is_extractor,
    const sim::components::BuildingFootprint& footprint)
{
    if (is_extractor) {
        return extractor_sprite_width_scale(footprint);
    }

    return town_center_sprite_width_scale(footprint);
}

[[nodiscard]] sim::components::BuildingFootprint resolve_registry_building_footprint(
    const entt::registry& registry,
    const entt::entity entity)
{
    sim::components::BuildingFootprint footprint{};
    if (registry.any_of<sim::components::BuildingFootprint>(entity)) {
        footprint = registry.get<sim::components::BuildingFootprint>(entity);
    }

    return sim::components::effective_building_footprint(
        footprint,
        registry.any_of<sim::components::TownCenterTag>(entity));
}

[[nodiscard]] sim::components::BuildingFootprint resolve_pose_building_footprint(
    const RenderEntityPose& pose)
{
    return sim::components::effective_building_footprint(
        sim::components::BuildingFootprint{pose.footprint_width, pose.footprint_height},
        pose.is_town_center);
}

void resolve_tile_appearance(
    const sim::components::TileType live_tile,
    const int live_forest_wood,
    const bool use_memory,
    const sim::components::TileType memory_tile,
    const int memory_forest_wood,
    sim::components::TileType& tile,
    int& forest_wood,
    float& extrude_base_r,
    float& extrude_base_g,
    float& extrude_base_b,
    float& extrude)
{
    tile = use_memory ? memory_tile : live_tile;
    forest_wood = use_memory ? memory_forest_wood : live_forest_wood;

    extrude_base_r = 0.20F;
    extrude_base_g = 0.32F;
    extrude_base_b = 0.18F;
    extrude = 0.0F;
    if (tile == sim::components::TileType::Forest && forest_wood > 0) {
        extrude_base_r = 0.10F;
        extrude_base_g = 0.22F;
        extrude_base_b = 0.10F;
        extrude = constants::RENDER_FOREST_EXTRUDE;
    }
}

[[nodiscard]] std::uint64_t hash_ground_field(
    const std::vector<sim::components::GroundType>& ground,
    const int map_width,
    const int map_height)
{
    std::uint64_t hash = constants::FNV1A_OFFSET_BASIS;
    const auto mix = [&](const std::uint64_t value) {
        hash ^= value;
        hash *= constants::FNV1A_PRIME;
    };
    mix(static_cast<std::uint64_t>(map_width));
    mix(static_cast<std::uint64_t>(map_height));
    mix(static_cast<std::uint64_t>(ground.size()));
    for (const sim::components::GroundType cell : ground) {
        mix(static_cast<std::uint64_t>(static_cast<std::uint8_t>(cell)));
    }
    return hash;
}

struct BiomeTileCornerWeights {
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};
    float left{0.0F};

    [[nodiscard]] float max_weight() const
    {
        return std::max(std::max(top, right), std::max(bottom, left));
    }
};

[[nodiscard]] float packed_biome_vertex_weight(
    const std::vector<float>& vertex_weight,
    const int map_width,
    const int map_height,
    const int vertex_x,
    const int vertex_y,
    const int type_index)
{
    const int vertex_width = map_width + 1;
    const int vertex_height = map_height + 1;
    if (vertex_x < 0 || vertex_y < 0 || vertex_x >= vertex_width || vertex_y >= vertex_height
        || type_index < 0 || type_index >= constants::GROUND_TYPE_COUNT || vertex_weight.empty()) {
        return 0.0F;
    }

    return vertex_weight[static_cast<std::size_t>(
        (vertex_y * vertex_width + vertex_x) * constants::GROUND_TYPE_COUNT + type_index)];
}

[[nodiscard]] BiomeTileCornerWeights biome_tile_corner_weights(
    const std::vector<float>& vertex_weight,
    const int map_width,
    const int map_height,
    const int grid_x,
    const int grid_y,
    const int type_index)
{
    return BiomeTileCornerWeights{
        packed_biome_vertex_weight(
            vertex_weight,
            map_width,
            map_height,
            grid_x,
            grid_y,
            type_index),
        packed_biome_vertex_weight(
            vertex_weight,
            map_width,
            map_height,
            grid_x + 1,
            grid_y,
            type_index),
        packed_biome_vertex_weight(
            vertex_weight,
            map_width,
            map_height,
            grid_x + 1,
            grid_y + 1,
            type_index),
        packed_biome_vertex_weight(
            vertex_weight,
            map_width,
            map_height,
            grid_x,
            grid_y + 1,
            type_index),
    };
}

[[nodiscard]] std::pair<float, float> tree_sprite_offset_for_kind(const SceneTextureKind kind)
{
    switch (kind) {
    case SceneTextureKind::OakForestLarge:
    case SceneTextureKind::DarkenedOakForestLarge:
        return {constants::RENDER_OAK_LARGE_SPRITE_OFFSET_X, constants::RENDER_OAK_LARGE_SPRITE_OFFSET_Y};
    case SceneTextureKind::OakForestMedium:
    case SceneTextureKind::DarkenedOakForestMedium:
        return {constants::RENDER_OAK_MEDIUM_SPRITE_OFFSET_X, constants::RENDER_OAK_MEDIUM_SPRITE_OFFSET_Y};
    case SceneTextureKind::OakForestSmall:
    case SceneTextureKind::DarkenedOakForestSmall:
        return {constants::RENDER_OAK_SMALL_SPRITE_OFFSET_X, constants::RENDER_OAK_SMALL_SPRITE_OFFSET_Y};
    case SceneTextureKind::PinesForestSmall:
    case SceneTextureKind::DarkenedPinesForestSmall:
        return {constants::RENDER_PINES_SMALL_SPRITE_OFFSET_X, constants::RENDER_PINES_SMALL_SPRITE_OFFSET_Y};
    case SceneTextureKind::PinesForestMedium:
    case SceneTextureKind::DarkenedPinesForestMedium:
        return {constants::RENDER_PINES_MEDIUM_SPRITE_OFFSET_X, constants::RENDER_PINES_MEDIUM_SPRITE_OFFSET_Y};
    case SceneTextureKind::PinesForestLarge:
    case SceneTextureKind::DarkenedPinesForestLarge:
        return {constants::RENDER_PINES_LARGE_SPRITE_OFFSET_X, constants::RENDER_PINES_LARGE_SPRITE_OFFSET_Y};
    default:
        return {constants::RENDER_TREE_SPRITE_OFFSET_X, constants::RENDER_TREE_SPRITE_OFFSET_Y};
    }
}

} // namespace

bool init_gl_loader()
{
    return gladLoadGLLoader(reinterpret_cast<GLADloadproc>(sf::Context::getFunction)) != 0;
}

GameRenderer::SceneVertex GameRenderer::make_scene_vertex(
    const float world_x,
    const float world_y,
    const float world_z,
    const float r,
    const float g,
    const float b) const
{
    const auto clip = camera_.world_to_clip(world_x, world_y, world_z);
    return SceneVertex{clip[0], clip[1], clip[2], r, g, b};
}

GameRenderer::GameRenderer()
{
    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to initialize OpenGL loader");
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    create_shader_program();
    create_textured_shader_program();
    create_scene_batch_gl();
    create_textured_scene_batch_gl();
    scene_batch_.reserve(131072U);
    textured_scene_batch_.reserve(65536U);
    scene_textures_.load(aoa::core::default_assets_directory());
}

GameRenderer::~GameRenderer()
{
    destroy_gl_objects();
}

void GameRenderer::create_shader_program()
{
    const unsigned int vertex_shader = compile_shader(GL_VERTEX_SHADER, SCENE_VERTEX_SHADER);
    const unsigned int fragment_shader = compile_shader(GL_FRAGMENT_SHADER, SCENE_FRAGMENT_SHADER);
    scene_shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
}

void GameRenderer::create_textured_shader_program()
{
    const unsigned int vertex_shader =
        compile_shader(GL_VERTEX_SHADER, TEXTURED_SCENE_VERTEX_SHADER);
    const unsigned int fragment_shader =
        compile_shader(GL_FRAGMENT_SHADER, TEXTURED_SCENE_FRAGMENT_SHADER);
    textured_scene_shader_program_ = link_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
}

void GameRenderer::create_scene_batch_gl()
{
    glGenVertexArrays(1, &scene_vao_);
    glGenBuffers(1, &scene_vbo_);

    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(SceneVertex),
        reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0U);
}

void GameRenderer::create_textured_scene_batch_gl()
{
    glGenVertexArrays(1, &textured_scene_vao_);
    glGenBuffers(1, &textured_scene_vbo_);

    glBindVertexArray(textured_scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, textured_scene_vbo_);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TexturedSceneVertex),
        reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TexturedSceneVertex),
        reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        2,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(TexturedSceneVertex),
        reinterpret_cast<void*>(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0U);
}

void GameRenderer::destroy_present_target() const
{
    if (present_color_ != 0U) {
        glDeleteTextures(1, &present_color_);
        present_color_ = 0U;
    }
    if (present_depth_ != 0U) {
        glDeleteRenderbuffers(1, &present_depth_);
        present_depth_ = 0U;
    }
    if (present_fbo_ != 0U) {
        glDeleteFramebuffers(1, &present_fbo_);
        present_fbo_ = 0U;
    }
    present_width_ = 0;
    present_height_ = 0;
}

bool GameRenderer::ensure_present_target() const
{
    const int width = static_cast<int>(window_size_.x);
    const int height = static_cast<int>(window_size_.y);
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (present_fbo_ != 0U && present_color_ != 0U && present_depth_ != 0U
        && present_width_ == width && present_height_ == height) {
        return true;
    }

    destroy_present_target();
    glGenTextures(1, &present_color_);
    glBindTexture(GL_TEXTURE_2D, present_color_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    glBindTexture(GL_TEXTURE_2D, 0U);

    glGenRenderbuffers(1, &present_depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, present_depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0U);

    glGenFramebuffers(1, &present_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, present_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, present_color_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, present_depth_);
    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0U);
    if (!complete) {
        destroy_present_target();
        return false;
    }

    present_width_ = width;
    present_height_ = height;
    return true;
}

void GameRenderer::present_target_to_window() const
{
    if (present_fbo_ == 0U || present_width_ <= 0 || present_height_ <= 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0U);
        return;
    }

    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, present_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0U);
    glBlitFramebuffer(
        0,
        0,
        present_width_,
        present_height_,
        0,
        0,
        present_width_,
        present_height_,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0U);
}

void GameRenderer::destroy_gl_objects()
{
    destroy_present_target();
    scene_textures_.destroy_gl_resources();

    if (textured_scene_vbo_ != 0U) {
        glDeleteBuffers(1, &textured_scene_vbo_);
        textured_scene_vbo_ = 0U;
    }

    if (textured_scene_vao_ != 0U) {
        glDeleteVertexArrays(1, &textured_scene_vao_);
        textured_scene_vao_ = 0U;
    }

    if (textured_scene_shader_program_ != 0U) {
        glDeleteProgram(textured_scene_shader_program_);
        textured_scene_shader_program_ = 0U;
    }
    if (scene_vbo_ != 0U) {
        glDeleteBuffers(1, &scene_vbo_);
        scene_vbo_ = 0U;
    }

    if (scene_vao_ != 0U) {
        glDeleteVertexArrays(1, &scene_vao_);
        scene_vao_ = 0U;
    }

    if (scene_shader_program_ != 0U) {
        glDeleteProgram(scene_shader_program_);
        scene_shader_program_ = 0U;
    }
}

void GameRenderer::resize(const sf::Vector2u window_size, const bool preserve_camera_view)
{
    window_size_ = window_size;
    camera_.set_window_size(window_size);
    if (!preserve_camera_view) {
        camera_.reset_view();
        map_framed_ = false;
    }
    glViewport(0, 0, static_cast<GLsizei>(window_size.x), static_cast<GLsizei>(window_size.y));
}

void GameRenderer::set_local_player_slot(const std::uint8_t player_slot)
{
    if (local_player_slot_ == player_slot) {
        return;
    }

    local_player_slot_ = player_slot;
    map_framed_ = false;
    cached_hud_snapshot_tick_ = ~0ULL;
}

void GameRenderer::reset_camera_frame()
{
    map_framed_ = false;
}

void GameRenderer::update_camera(const float delta_seconds)
{
    camera_.update(delta_seconds);
}

void GameRenderer::toggle_grid_lines()
{
    show_grid_lines_ = !show_grid_lines_;
}

void GameRenderer::toggle_fog_of_war()
{
    fog_of_war_enabled_ = !fog_of_war_enabled_;
}

void GameRenderer::toggle_perf_hud()
{
    show_perf_hud_ = !show_perf_hud_;
}

void GameRenderer::toggle_selection_debug()
{
    show_selection_debug_ = !show_selection_debug_;
}

void GameRenderer::toggle_hitboxes()
{
    show_hitboxes_ = !show_hitboxes_;
}

bool GameRenderer::local_player_has_seen_building(const entt::entity entity) const
{
    return building_sight_memory_.find(entity) != nullptr;
}

void GameRenderer::reset_graphics_context(const sf::Vector2u window_size)
{
    destroy_gl_objects();
    hud_overlay_.invalidate_gl_cache();

    if (!init_gl_loader()) {
        throw std::runtime_error("Failed to reinitialize OpenGL loader");
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    create_shader_program();
    create_textured_shader_program();
    create_scene_batch_gl();
    create_textured_scene_batch_gl();
    scene_textures_.load(aoa::core::default_assets_directory());
    resize(window_size, true);
}

void GameRenderer::center_camera_on_world_keep_zoom(const float world_x, const float world_z)
{
    camera_.center_on_world_keep_zoom(world_x, world_z);
}

void GameRenderer::pan_camera(const float delta_x, const float delta_y)
{
    camera_.pan(delta_x, delta_y);
}

void GameRenderer::step_zoom_camera(
    const int direction,
    const float anchor_screen_x,
    const float anchor_screen_y)
{
    camera_.step_zoom(direction, anchor_screen_x, anchor_screen_y);
    camera_.clamp_to_map_bounds();
}

std::optional<core::GridPos> GameRenderer::screen_to_grid(
    const float screen_x,
    const float screen_y) const
{
    return camera_.screen_to_grid(screen_x, screen_y);
}

std::pair<float, float> GameRenderer::screen_to_world_xz(
    const float screen_x,
    const float screen_y) const
{
    return camera_.screen_to_world_xz(screen_x, screen_y);
}

sf::Vector2f GameRenderer::world_to_screen(
    const float world_x,
    const float world_y,
    const float world_z) const
{
    return camera_.world_to_screen(world_x, world_y, world_z);
}

std::pair<float, float> GameRenderer::unit_render_world_xz(
    const entt::registry& registry,
    const entt::entity entity,
    const float interpolation_alpha) const
{
    const float max_extrapolation_alpha = net::constants::LOCKSTEP_MAX_RENDER_EXTRAPOLATION_ALPHA;

    if (registry.all_of<sim::components::MoveSegment, sim::components::WorldPosition>(entity)) {
        const auto& segment = registry.get<sim::components::MoveSegment>(entity);
        if (segment.ticks_total > 0) {
            const float progress = (static_cast<float>(segment.ticks_elapsed)
                    + std::clamp(interpolation_alpha, 0.0F, 1.0F + max_extrapolation_alpha))
                / static_cast<float>(segment.ticks_total);
            const math::Fixed t = math::Fixed::from_float(std::clamp(progress, 0.0F, 1.0F));
            return {
                math::fixed_lerp(segment.from_x, segment.to_x, t).to_float(),
                math::fixed_lerp(segment.from_y, segment.to_y, t).to_float(),
            };
        }
    }

    if (registry.all_of<sim::components::PreviousWorldPosition, sim::components::WorldPosition>(entity)) {
        const auto& previous = registry.get<sim::components::PreviousWorldPosition>(entity);
        const auto& current = registry.get<sim::components::WorldPosition>(entity);

        if (interpolation_alpha <= 1.0F) {
            const math::Fixed t = math::Fixed::from_float(std::clamp(interpolation_alpha, 0.0F, 1.0F));
            return {
                math::fixed_lerp(previous.x, current.x, t).to_float(),
                math::fixed_lerp(previous.y, current.y, t).to_float(),
            };
        }

        const float extrapolation_alpha = std::min(interpolation_alpha - 1.0F, max_extrapolation_alpha);
        const math::Fixed delta_x = current.x - previous.x;
        const math::Fixed delta_y = current.y - previous.y;
        const math::Fixed extrapolation = math::Fixed::from_float(extrapolation_alpha);
        return {
            (current.x + delta_x * extrapolation).to_float(),
            (current.y + delta_y * extrapolation).to_float(),
        };
    }

    if (registry.any_of<sim::components::WorldPosition>(entity)) {
        const auto& world = registry.get<sim::components::WorldPosition>(entity);
        return {world.x.to_float(), world.y.to_float()};
    }

    if (registry.any_of<sim::components::GridPosition>(entity)) {
        const auto& cell = registry.get<sim::components::GridPosition>(entity).cell;
        return {
            static_cast<float>(cell.x) + 0.5F,
            static_cast<float>(cell.y) + 0.5F,
        };
    }

    return {0.0F, 0.0F};
}

sf::Vector2f GameRenderer::tile_center_screen(
    const int grid_x,
    const int grid_y,
    const float world_y) const
{
    return world_to_screen(
        static_cast<float>(grid_x) + 0.5F,
        world_y,
        static_cast<float>(grid_y) + 0.5F);
}

sf::Vector2f GameRenderer::unit_screen_position(
    const entt::registry& registry,
    const entt::entity entity,
    const float interpolation_alpha) const
{
    const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
    return world_to_screen(world_x, constants::RENDER_ENTITY_BASE_LIFT, world_z);
}

float GameRenderer::selection_pick_radius_px() const
{
    return std::max(
        constants::SELECTION_PICK_RADIUS_TILES * camera_.tile_width(),
        constants::SELECTION_PICK_MIN_RADIUS_PX);
}

std::optional<core::GridPos> GameRenderer::find_local_town_center_cell(
    const sim::Simulation* simulation,
    const SimRenderSnapshot* snapshot) const
{
    if (snapshot != nullptr) {
        for (const RenderEntityPose& pose : snapshot->buildings) {
            if (pose.is_town_center && pose.player_slot == local_player_slot_) {
                return core::GridPos{
                    pose.grid_x + pose.footprint_width / 2,
                    pose.grid_y + pose.footprint_height / 2,
                };
            }
        }
    }

    if (simulation != nullptr) {
        const entt::registry& registry = simulation->registry();
        const auto town_center_view = registry.view<
            sim::components::TownCenterTag,
            sim::components::PlayerOwnedTag,
            sim::components::GridPosition>();
        for (const entt::entity entity : town_center_view) {
            if (sim::components::entity_player_slot(registry, entity) != local_player_slot_) {
                continue;
            }

            const auto& anchor = town_center_view.get<sim::components::GridPosition>(entity);
            if (registry.any_of<sim::components::BuildingFootprint>(entity)) {
                return sim::components::building_center_cell(
                    anchor,
                    registry.get<sim::components::BuildingFootprint>(entity));
            }

            return anchor.cell;
        }
    }

    return std::nullopt;
}

std::optional<core::GridPos> GameRenderer::find_first_town_center_cell(
    const sim::Simulation* simulation,
    const SimRenderSnapshot* snapshot) const
{
    if (snapshot != nullptr) {
        for (const RenderEntityPose& pose : snapshot->buildings) {
            if (pose.is_town_center) {
                return core::GridPos{
                    pose.grid_x + pose.footprint_width / 2,
                    pose.grid_y + pose.footprint_height / 2,
                };
            }
        }
    }

    if (simulation != nullptr) {
        const entt::registry& registry = simulation->registry();
        const auto town_center_view = registry.view<
            sim::components::TownCenterTag,
            sim::components::GridPosition>();
        if (town_center_view.begin() != town_center_view.end()) {
            return town_center_view.get<sim::components::GridPosition>(*town_center_view.begin()).cell;
        }
    }

    return std::nullopt;
}

void GameRenderer::center_camera_on_grid_cell(const core::GridPos& cell)
{
    camera_.center_on_world(
        static_cast<float>(cell.x) + 0.5F,
        static_cast<float>(cell.y) + 0.5F,
        constants::CAMERA_CLASSIC_START_ZOOM,
        constants::CAMERA_CLASSIC_START_ZOOM_LEVEL_INDEX);
}

void GameRenderer::center_camera_on_map_center(const int map_width, const int map_height)
{
    camera_.center_on_world(
        static_cast<float>(map_width) * 0.5F,
        static_cast<float>(map_height) * 0.5F,
        constants::CAMERA_CLASSIC_START_ZOOM,
        constants::CAMERA_CLASSIC_START_ZOOM_LEVEL_INDEX);
}

void GameRenderer::try_frame_player_start(
    const int map_width,
    const int map_height,
    const sim::Simulation* simulation,
    const SimRenderSnapshot* snapshot)
{
    camera_.set_map_size(map_width, map_height);

    if (map_framed_) {
        return;
    }

    const std::optional<core::GridPos> town_center_cell =
        find_local_town_center_cell(simulation, snapshot);
    if (town_center_cell.has_value()) {
        center_camera_on_grid_cell(*town_center_cell);
    }
    else {
        const std::optional<core::GridPos> first_town_center_cell =
            find_first_town_center_cell(simulation, snapshot);
        if (first_town_center_cell.has_value()) {
            center_camera_on_grid_cell(*first_town_center_cell);
        }
        else {
            center_camera_on_map_center(map_width, map_height);
        }
    }

    camera_.clamp_to_map_bounds();
    map_framed_ = true;
}

void GameRenderer::draw_screen_rect_outline(
    const float screen_x0,
    const float screen_y0,
    const float screen_x1,
    const float screen_y1,
    const float r,
    const float g,
    const float b) const
{
    draw_screen_rect_outline_immediate(screen_x0, screen_y0, screen_x1, screen_y1, r, g, b);
}

void GameRenderer::draw_screen_rect_outline_immediate(
    const float screen_x0,
    const float screen_y0,
    const float screen_x1,
    const float screen_y1,
    const float r,
    const float g,
    const float b) const
{
    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);
    if (window_width <= 0.0F || window_height <= 0.0F) {
        return;
    }

    const auto to_ndc_x = [window_width](const float screen_x) {
        return (screen_x / window_width) * 2.0F - 1.0F;
    };
    const auto to_ndc_y = [window_height](const float screen_y) {
        return 1.0F - (screen_y / window_height) * 2.0F;
    };

    const float left = to_ndc_x(std::min(screen_x0, screen_x1));
    const float right = to_ndc_x(std::max(screen_x0, screen_x1));
    const float top = to_ndc_y(std::min(screen_y0, screen_y1));
    const float bottom = to_ndc_y(std::max(screen_y0, screen_y1));
    const float depth = constants::HUD_CLIP_Z;

    const std::array<SceneVertex, 8> line_vertices = {
        SceneVertex{left, top, depth, r, g, b},
        SceneVertex{right, top, depth, r, g, b},
        SceneVertex{right, top, depth, r, g, b},
        SceneVertex{right, bottom, depth, r, g, b},
        SceneVertex{right, bottom, depth, r, g, b},
        SceneVertex{left, bottom, depth, r, g, b},
        SceneVertex{left, bottom, depth, r, g, b},
        SceneVertex{left, top, depth, r, g, b},
    };

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(line_vertices.size() * sizeof(SceneVertex)),
        line_vertices.data(),
        GL_DYNAMIC_DRAW);
    glUseProgram(scene_shader_program_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(line_vertices.size()));
    glBindVertexArray(0U);
}

void GameRenderer::draw_screen_line_immediate(
    const float screen_x0,
    const float screen_y0,
    const float screen_x1,
    const float screen_y1,
    const float r,
    const float g,
    const float b) const
{
    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);
    if (window_width <= 0.0F || window_height <= 0.0F) {
        return;
    }

    const auto to_ndc_x = [window_width](const float screen_x) {
        return (screen_x / window_width) * 2.0F - 1.0F;
    };
    const auto to_ndc_y = [window_height](const float screen_y) {
        return 1.0F - (screen_y / window_height) * 2.0F;
    };

    const std::array<SceneVertex, 2> line_vertices = {
        SceneVertex{to_ndc_x(screen_x0), to_ndc_y(screen_y0), -0.99F, r, g, b},
        SceneVertex{to_ndc_x(screen_x1), to_ndc_y(screen_y1), -0.99F, r, g, b},
    };

    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(line_vertices.size() * sizeof(SceneVertex)),
        line_vertices.data(),
        GL_DYNAMIC_DRAW);
    glUseProgram(scene_shader_program_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(line_vertices.size()));
    glBindVertexArray(0U);
}

void GameRenderer::draw_scene_line_xz(
    const float x0,
    const float z0,
    const float x1,
    const float z1,
    const float y,
    const float r,
    const float g,
    const float b,
    const float width) const
{
    const float dx = x1 - x0;
    const float dz = z1 - z0;
    const float length = std::sqrt((dx * dx) + (dz * dz));
    if (length <= 1e-6F) {
        return;
    }

    const float px = (-dz / length) * width * 0.5F;
    const float pz = (dx / length) * width * 0.5F;

    draw_scene_quad(
        x0 + px,
        y,
        z0 + pz,
        x1 + px,
        y,
        z1 + pz,
        x1 - px,
        y,
        z1 - pz,
        x0 - px,
        y,
        z0 - pz,
        r,
        g,
        b,
        1.0F);
}

void GameRenderer::draw_scene_rect_outline(
    const float x0,
    const float z0,
    const float x1,
    const float z1,
    const float y,
    const float line_width,
    const float r,
    const float g,
    const float b) const
{
    draw_scene_line_xz(x0, z0, x1, z0, y, r, g, b, line_width);
    draw_scene_line_xz(x1, z0, x1, z1, y, r, g, b, line_width);
    draw_scene_line_xz(x1, z1, x0, z1, y, r, g, b, line_width);
    draw_scene_line_xz(x0, z1, x0, z0, y, r, g, b, line_width);
}

void GameRenderer::project_unit_screen_bounds(
    const float world_x,
    const float world_z,
    float& min_screen_x,
    float& min_screen_y,
    float& max_screen_x,
    float& max_screen_y) const
{
    min_screen_x = std::numeric_limits<float>::max();
    min_screen_y = std::numeric_limits<float>::max();
    max_screen_x = std::numeric_limits<float>::lowest();
    max_screen_y = std::numeric_limits<float>::lowest();

    const auto expand = [&](const float wx, const float wy, const float wz) {
        const sf::Vector2f screen = world_to_screen(wx, wy, wz);
        min_screen_x = std::min(min_screen_x, screen.x);
        min_screen_y = std::min(min_screen_y, screen.y);
        max_screen_x = std::max(max_screen_x, screen.x);
        max_screen_y = std::max(max_screen_y, screen.y);
    };

    const float base = constants::RENDER_ENTITY_BASE_LIFT;
    const float top = base + constants::RENDER_UNIT_HEIGHT;
    const float radius = constants::RENDER_UNIT_RADIUS;
    constexpr float k_two_pi = 6.2831853F;
    const int segments = constants::RENDER_CYLINDER_SEGMENTS;
    const float angle_step = k_two_pi / static_cast<float>(segments);

    expand(world_x, top, world_z);
    for (int segment_index = 0; segment_index <= segments; ++segment_index) {
        const float angle = angle_step * static_cast<float>(segment_index);
        const float offset_x = std::cos(angle) * radius;
        const float offset_z = std::sin(angle) * radius;
        expand(world_x + offset_x, base, world_z + offset_z);
        expand(world_x + offset_x, top, world_z + offset_z);
    }
}

void GameRenderer::occluder_tile_screen_rect(
    const int grid_x,
    const int grid_y,
    const bool is_building,
    float& min_screen_x,
    float& min_screen_y,
    float& max_screen_x,
    float& max_screen_y) const
{
    const IsoTileScreenCorners corners = camera_.grid_iso_corners(grid_x, grid_y);
    const float tile_width = camera_.tile_width()
        * (is_building
            ? static_cast<float>(constants::TOWN_CENTER_FOOTPRINT_TILES)
            : constants::RENDER_TREE_SPRITE_WIDTH_SCALE);
    const SceneTextureKind aspect_kind = is_building
        ? SceneTextureKind::TownCenterFriendly
        : SceneTextureKind::OakForestLarge;
    const float vertical_extent_scale = is_building
        ? constants::RENDER_BUILDING_OCCLUSION_SCISSOR_SCALE
        : constants::RENDER_TREE_OCCLUSION_SCISSOR_SCALE;
    const float sprite_height = tile_width * scene_textures_.aspect_ratio(aspect_kind)
        * vertical_extent_scale;

    min_screen_x = std::min(
        std::min(corners.top.x, corners.right.x),
        std::min(corners.bottom.x, corners.left.x));
    max_screen_x = std::max(
        std::max(corners.top.x, corners.right.x),
        std::max(corners.bottom.x, corners.left.x));
    min_screen_y = corners.top.y - sprite_height;
    max_screen_y = std::max(
        std::max(corners.top.y, corners.right.y),
        std::max(corners.bottom.y, corners.left.y));
}

bool GameRenderer::unit_screen_overlaps_occluder_tile(
    const float world_x,
    const float world_z,
    const int grid_x,
    const int grid_y,
    const bool is_building) const
{
    float unit_min_x = 0.0F;
    float unit_min_y = 0.0F;
    float unit_max_x = 0.0F;
    float unit_max_y = 0.0F;
    project_unit_screen_bounds(world_x, world_z, unit_min_x, unit_min_y, unit_max_x, unit_max_y);

    float tile_min_x = 0.0F;
    float tile_min_y = 0.0F;
    float tile_max_x = 0.0F;
    float tile_max_y = 0.0F;
    occluder_tile_screen_rect(grid_x, grid_y, is_building, tile_min_x, tile_min_y, tile_max_x, tile_max_y);

    return unit_max_x >= tile_min_x
        && unit_min_x <= tile_max_x
        && unit_max_y >= tile_min_y
        && unit_min_y <= tile_max_y;
}

bool GameRenderer::unit_is_occluded_by_tile(
    const float world_x,
    const float world_z,
    const int grid_x,
    const int grid_y,
    const bool is_building) const
{
    if (!unit_screen_overlaps_occluder_tile(world_x, world_z, grid_x, grid_y, is_building)) {
        return false;
    }

    const sf::Vector2f unit_base = world_to_screen(
        world_x,
        constants::RENDER_ENTITY_BASE_LIFT,
        world_z);
    const sf::Vector2f unit_top = world_to_screen(
        world_x,
        constants::RENDER_ENTITY_BASE_LIFT + constants::RENDER_UNIT_HEIGHT,
        world_z);

    float tile_min_x = 0.0F;
    float tile_min_y = 0.0F;
    float tile_max_x = 0.0F;
    float tile_max_y = 0.0F;
    occluder_tile_screen_rect(grid_x, grid_y, is_building, tile_min_x, tile_min_y, tile_max_x, tile_max_y);

    if (unit_base.x < tile_min_x || unit_base.x > tile_max_x) {
        return false;
    }

    const float unit_mid_y = (unit_base.y + unit_top.y) * 0.5F;
    if (unit_mid_y >= tile_max_y) {
        return false;
    }

    return true;
}

void GameRenderer::draw_unit_projected_silhouette_outline_immediate(
    const float world_x,
    const float world_z,
    const float r,
    const float g,
    const float b) const
{
    const float base = constants::RENDER_ENTITY_BASE_LIFT;
    const float top = base + constants::RENDER_UNIT_HEIGHT;
    const float radius = constants::RENDER_UNIT_RADIUS;

    constexpr float k_two_pi = 6.2831853F;
    const int segments = constants::RENDER_CYLINDER_SEGMENTS;
    const float angle_step = k_two_pi / static_cast<float>(segments);

    std::vector<sf::Vector2f> top_ring{};
    std::vector<sf::Vector2f> bottom_ring{};
    top_ring.reserve(static_cast<std::size_t>(segments));
    bottom_ring.reserve(static_cast<std::size_t>(segments));

    for (int segment_index = 0; segment_index < segments; ++segment_index) {
        const float angle = angle_step * static_cast<float>(segment_index);
        const float offset_x = std::cos(angle) * radius;
        const float offset_z = std::sin(angle) * radius;
        top_ring.push_back(world_to_screen(world_x + offset_x, top, world_z + offset_z));
        bottom_ring.push_back(world_to_screen(world_x + offset_x, base, world_z + offset_z));
    }

    for (int segment_index = 0; segment_index < segments; ++segment_index) {
        const int next_index = (segment_index + 1) % segments;
        draw_screen_line_immediate(
            top_ring[static_cast<std::size_t>(segment_index)].x,
            top_ring[static_cast<std::size_t>(segment_index)].y,
            top_ring[static_cast<std::size_t>(next_index)].x,
            top_ring[static_cast<std::size_t>(next_index)].y,
            r,
            g,
            b);
        draw_screen_line_immediate(
            bottom_ring[static_cast<std::size_t>(segment_index)].x,
            bottom_ring[static_cast<std::size_t>(segment_index)].y,
            bottom_ring[static_cast<std::size_t>(next_index)].x,
            bottom_ring[static_cast<std::size_t>(next_index)].y,
            r,
            g,
            b);
    }

    int left_index = 0;
    int right_index = 0;
    for (int segment_index = 1; segment_index < segments; ++segment_index) {
        if (top_ring[static_cast<std::size_t>(segment_index)].x
            < top_ring[static_cast<std::size_t>(left_index)].x) {
            left_index = segment_index;
        }
        if (top_ring[static_cast<std::size_t>(segment_index)].x
            > top_ring[static_cast<std::size_t>(right_index)].x) {
            right_index = segment_index;
        }
    }

    draw_screen_line_immediate(
        bottom_ring[static_cast<std::size_t>(left_index)].x,
        bottom_ring[static_cast<std::size_t>(left_index)].y,
        top_ring[static_cast<std::size_t>(left_index)].x,
        top_ring[static_cast<std::size_t>(left_index)].y,
        r,
        g,
        b);
    draw_screen_line_immediate(
        bottom_ring[static_cast<std::size_t>(right_index)].x,
        bottom_ring[static_cast<std::size_t>(right_index)].y,
        top_ring[static_cast<std::size_t>(right_index)].x,
        top_ring[static_cast<std::size_t>(right_index)].y,
        r,
        g,
        b);
}

void GameRenderer::apply_team_color(
    const float base_r,
    const float base_g,
    const float base_b,
    float& r,
    float& g,
    float& b) const
{
    r = base_r;
    g = base_g;
    b = base_b;
}

void GameRenderer::append_scene_vertices(
    const SceneVertex* vertices,
    const std::size_t vertex_count) const
{
    scene_batch_.insert(scene_batch_.end(), vertices, vertices + vertex_count);
}

void GameRenderer::flush_scene_batch() const
{
    if (scene_batch_.empty() || scene_vao_ == 0U || scene_vbo_ == 0U) {
        return;
    }

    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(scene_batch_.size() * sizeof(SceneVertex)),
        scene_batch_.data(),
        GL_DYNAMIC_DRAW);
    glUseProgram(scene_shader_program_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(scene_batch_.size()));
    glBindVertexArray(0U);
    scene_batch_.clear();
}

void GameRenderer::append_textured_vertices(
    const TexturedSceneVertex* vertices,
    const std::size_t vertex_count) const
{
    textured_scene_batch_.insert(textured_scene_batch_.end(), vertices, vertices + vertex_count);
}

void GameRenderer::flush_textured_scene_batch(const unsigned int texture_id) const
{
    if (textured_scene_batch_.empty() || textured_scene_vao_ == 0U || textured_scene_vbo_ == 0U
        || textured_scene_shader_program_ == 0U || texture_id == 0U) {
        textured_scene_batch_.clear();
        return;
    }

    enable_rgb_blend_keep_framebuffer_opaque();
    glBindVertexArray(textured_scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, textured_scene_vbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizei>(textured_scene_batch_.size() * sizeof(TexturedSceneVertex)),
        textured_scene_batch_.data(),
        GL_DYNAMIC_DRAW);
    glUseProgram(textured_scene_shader_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(glGetUniformLocation(textured_scene_shader_program_, "scene_texture"), 0);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(textured_scene_batch_.size()));
    glBindTexture(GL_TEXTURE_2D, 0U);
    glBindVertexArray(0U);
    glDisable(GL_BLEND);
    textured_scene_batch_.clear();
}

SceneTextureKind GameRenderer::ground_texture_for_ground(
    const sim::components::GroundType ground) const
{
    if (ground == sim::components::GroundType::Snow
        && scene_textures_.texture_id(SceneTextureKind::Snow) != 0U) {
        return SceneTextureKind::Snow;
    }

    if (ground == sim::components::GroundType::Sand
        && scene_textures_.texture_id(SceneTextureKind::Sand) != 0U) {
        return SceneTextureKind::Sand;
    }

    return SceneTextureKind::Grass;
}

const std::vector<float>& GameRenderer::ensure_fog_vertex_brightness(
    const std::uint64_t tick_count,
    const std::vector<std::uint8_t>& fog_explored,
    const int map_width,
    const int map_height,
    const std::uint8_t player_slot,
    const std::vector<sim::systems::VisionSource>& vision_sources) const
{
    if (fog_vertex_tick_ == tick_count && fog_vertex_player_slot_ == player_slot
        && fog_vertex_map_width_ == map_width && fog_vertex_map_height_ == map_height
        && !fog_vertex_cache_.empty()) {
        return fog_vertex_cache_;
    }

    fog_vertex_cache_ = build_fog_vertex_brightness(
        fog_explored,
        map_width,
        map_height,
        player_slot,
        vision_sources);
    fog_vertex_tick_ = tick_count;
    fog_vertex_player_slot_ = player_slot;
    fog_vertex_map_width_ = map_width;
    fog_vertex_map_height_ = map_height;
    return fog_vertex_cache_;
}

void GameRenderer::ensure_biome_vertex_weights(
    const std::vector<sim::components::GroundType>& ground,
    const int map_width,
    const int map_height) const
{
    if (map_width <= 0 || map_height <= 0 || ground.empty()) {
        biome_vertex_weight_.clear();
        biome_blend_map_width_ = 0;
        biome_blend_map_height_ = 0;
        biome_blend_ground_hash_ = 0U;
        return;
    }

    if (biome_blend_map_width_ == map_width && biome_blend_map_height_ == map_height
        && !biome_vertex_weight_.empty()) {
        return;
    }

    const std::uint64_t hash = hash_ground_field(ground, map_width, map_height);

    const int tile_count = map_width * map_height;
    const int vertex_width = map_width + 1;
    const int vertex_height = map_height + 1;
    biome_vertex_weight_.assign(
        static_cast<std::size_t>(vertex_width * vertex_height * constants::GROUND_TYPE_COUNT),
        0.0F);

    std::vector<float> type_field(static_cast<std::size_t>(tile_count), 0.0F);
    for (int type_index = 0; type_index < constants::GROUND_TYPE_COUNT; ++type_index) {
        const auto type = static_cast<sim::components::GroundType>(type_index);
        for (int cell_index = 0; cell_index < tile_count; ++cell_index) {
            const std::size_t index = static_cast<std::size_t>(cell_index);
            const sim::components::GroundType cell =
                index < ground.size() ? ground[index] : sim::components::GroundType::Grass;
            type_field[index] = (cell == type) ? 1.0F : 0.0F;
        }

        const std::vector<float> blurred = gaussian_blur_field(
            type_field,
            map_width,
            map_height,
            constants::BIOME_BLEND_RADIUS_TILES,
            constants::BIOME_BLEND_SIGMA);

        for (int vertex_y = 0; vertex_y < vertex_height; ++vertex_y) {
            for (int vertex_x = 0; vertex_x < vertex_width; ++vertex_x) {
                const float weight = sample_blurred_field_bilinear(
                    blurred,
                    map_width,
                    map_height,
                    static_cast<float>(vertex_x) - 0.5F,
                    static_cast<float>(vertex_y) - 0.5F);
                const std::size_t packed = static_cast<std::size_t>(
                    (vertex_y * vertex_width + vertex_x) * constants::GROUND_TYPE_COUNT
                    + type_index);
                biome_vertex_weight_[packed] = std::clamp(weight, 0.0F, 1.0F);
            }
        }
    }

    biome_blend_map_width_ = map_width;
    biome_blend_map_height_ = map_height;
    biome_blend_ground_hash_ = hash;
}

bool GameRenderer::iso_tile_intersects_window(
    const int grid_x,
    const int grid_y,
    const int pad_tiles) const
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return true;
    }

    const IsoTileScreenCorners corners = camera_.grid_iso_corners(grid_x, grid_y);
    const float pad_x = camera_.tile_width() * static_cast<float>(std::max(pad_tiles, 0));
    const float pad_y = camera_.tile_height() * static_cast<float>(std::max(pad_tiles, 0));
    const float min_x =
        std::min({corners.top.x, corners.right.x, corners.bottom.x, corners.left.x}) - pad_x;
    const float max_x =
        std::max({corners.top.x, corners.right.x, corners.bottom.x, corners.left.x}) + pad_x;
    const float min_y =
        std::min({corners.top.y, corners.right.y, corners.bottom.y, corners.left.y}) - pad_y;
    const float max_y =
        std::max({corners.top.y, corners.right.y, corners.bottom.y, corners.left.y}) + pad_y;
    return max_x >= 0.0F && min_x <= static_cast<float>(window_size_.x) && max_y >= 0.0F
        && min_y <= static_cast<float>(window_size_.y);
}

void GameRenderer::draw_batched_ground_tiles(
    const int map_width,
    const int map_height,
    const std::vector<sim::components::GroundType>& ground,
    const std::vector<std::uint8_t>& fog_visible,
    const std::vector<std::uint8_t>& fog_explored,
    const bool fog_enabled,
    const std::vector<float>* fog_vertex_brightness,
    const std::uint64_t tick_count) const
{
    if (map_width <= 0 || map_height <= 0
        || ground.size() != static_cast<std::size_t>(map_width * map_height)) {
        return;
    }

    unsigned int active_textured_batch = 0U;
    const auto flush_if_needed = [&](const unsigned int texture_id) {
        if (texture_id == 0U) {
            return;
        }

        if (active_textured_batch != 0U && active_textured_batch != texture_id) {
            flush_textured_scene_batch(active_textured_batch);
        }

        active_textured_batch = texture_id;
    };
    const auto finish_batches = [&]() {
        if (active_textured_batch != 0U) {
            flush_textured_scene_batch(active_textured_batch);
            active_textured_batch = 0U;
        }
    };

    const auto tile_brightness = [&](const int x, const int y) {
        if (!fog_enabled) {
            return 1.0F;
        }

        return fog_tile_brightness(
            fog_visible,
            fog_explored,
            map_width,
            map_height,
            local_player_slot_,
            x,
            y);
    };
    if (ground_draw_tick_ != tick_count || ground_draw_player_slot_ != local_player_slot_
        || ground_draw_map_width_ != map_width || ground_draw_map_height_ != map_height) {
        ground_draw_indices_.clear();
        ground_draw_indices_.reserve(static_cast<std::size_t>(map_width * map_height));
        for (int y = 0; y < map_height; ++y) {
            for (int x = 0; x < map_width; ++x) {
                ground_draw_indices_.push_back(y * map_width + x);
            }
        }

        ground_draw_tick_ = tick_count;
        ground_draw_player_slot_ = local_player_slot_;
        ground_draw_map_width_ = map_width;
        ground_draw_map_height_ = map_height;
    }

    static constexpr std::array<SceneTextureKind, constants::GROUND_TYPE_COUNT> pass_kinds{
        SceneTextureKind::Grass,
        SceneTextureKind::Sand,
        SceneTextureKind::Snow,
    };

    for (const SceneTextureKind pass_kind : pass_kinds) {
        const unsigned int texture_id = scene_textures_.texture_id(pass_kind);
        if (texture_id == 0U) {
            continue;
        }

        flush_if_needed(texture_id);
        for (const int packed : ground_draw_indices_) {
            const int x = packed % map_width;
            const int y = packed / map_width;
            if (!iso_tile_intersects_window(x, y, constants::RENDER_GROUND_SCREEN_CULL_PAD_TILES)) {
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(packed);
            const SceneTextureKind ground_kind = ground_texture_for_ground(
                index < ground.size() ? ground[index] : sim::components::GroundType::Grass);
            if (ground_kind != pass_kind) {
                continue;
            }

            draw_iso_diamond_sprite(
                x,
                y,
                ground_kind,
                tile_brightness(x, y),
                fog_vertex_brightness,
                map_width,
                map_height);
        }
    }

    finish_batches();

    ensure_biome_vertex_weights(ground, map_width, map_height);
    if (biome_vertex_weight_.empty()) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    for (const SceneTextureKind pass_kind : pass_kinds) {
        const unsigned int texture_id = scene_textures_.texture_id(pass_kind);
        if (texture_id == 0U) {
            continue;
        }

        flush_if_needed(texture_id);
        for (const int packed : ground_draw_indices_) {
            const int x = packed % map_width;
            const int y = packed / map_width;
            if (!iso_tile_intersects_window(x, y, constants::RENDER_GROUND_SCREEN_CULL_PAD_TILES)) {
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(packed);
            const sim::components::GroundType self =
                    index < ground.size() ? ground[index] : sim::components::GroundType::Grass;
                for (int type_index = 0; type_index < constants::GROUND_TYPE_COUNT; ++type_index) {
                    const auto overlay_ground =
                        static_cast<sim::components::GroundType>(type_index);
                    if (overlay_ground == self) {
                        continue;
                    }

                    const SceneTextureKind overlay_kind = ground_texture_for_ground(overlay_ground);
                    if (overlay_kind != pass_kind) {
                        continue;
                    }

                    const BiomeTileCornerWeights corners = biome_tile_corner_weights(
                        biome_vertex_weight_,
                        map_width,
                        map_height,
                        x,
                        y,
                        type_index);
                    if (corners.max_weight() < constants::BIOME_BLEND_MIN_CORNER_ALPHA) {
                        continue;
                    }

                    const std::array<float, 4> corner_alphas{
                        corners.top,
                        corners.right,
                        corners.bottom,
                        corners.left,
                    };
                    draw_iso_diamond_sprite(
                        x,
                        y,
                        overlay_kind,
                        tile_brightness(x, y),
                        fog_vertex_brightness,
                        map_width,
                        map_height,
                        1.0F,
                        corner_alphas.data());
                }
        }
    }

    finish_batches();
    glEnable(GL_DEPTH_TEST);
}

SceneTextureKind GameRenderer::forest_tree_texture_for_cell(
    const int grid_x,
    const int grid_y,
    const sim::components::GroundType ground) const
{
    const bool snow = ground == sim::components::GroundType::Snow;
    const int size_index =
        std::abs(grid_x * 3 + grid_y * 5) % constants::TREE_SIZE_VARIANT_COUNT;

    const SceneTextureKind small_kind =
        snow ? SceneTextureKind::PinesForestSmall : SceneTextureKind::OakForestSmall;
    const SceneTextureKind medium_kind =
        snow ? SceneTextureKind::PinesForestMedium : SceneTextureKind::OakForestMedium;
    const SceneTextureKind large_kind =
        snow ? SceneTextureKind::PinesForestLarge : SceneTextureKind::OakForestLarge;

    const SceneTextureKind preferred =
        size_index == 0 ? small_kind : (size_index == 1 ? medium_kind : large_kind);
    if (scene_textures_.texture_id(preferred) != 0U) {
        return preferred;
    }

    if (scene_textures_.texture_id(medium_kind) != 0U) {
        return medium_kind;
    }

    if (scene_textures_.texture_id(small_kind) != 0U) {
        return small_kind;
    }

    return large_kind;
}

float GameRenderer::tree_sprite_width_scale(const SceneTextureKind kind) const
{
    switch (kind) {
    case SceneTextureKind::OakForestSmall:
    case SceneTextureKind::DarkenedOakForestSmall:
    case SceneTextureKind::PinesForestSmall:
    case SceneTextureKind::DarkenedPinesForestSmall:
        return constants::RENDER_TREE_SMALL_SPRITE_WIDTH_SCALE;
    case SceneTextureKind::OakForestLarge:
    case SceneTextureKind::DarkenedOakForestLarge:
    case SceneTextureKind::PinesForestLarge:
    case SceneTextureKind::DarkenedPinesForestLarge:
        return constants::RENDER_TREE_LARGE_SPRITE_WIDTH_SCALE;
    default:
        return constants::RENDER_TREE_MEDIUM_SPRITE_WIDTH_SCALE;
    }
}

SceneTextureKind GameRenderer::gold_mine_texture_for_cell(const int grid_x, const int grid_y) const
{
    static constexpr SceneTextureKind variants[] = {
        SceneTextureKind::GoldMine0,
        SceneTextureKind::GoldMine1,
        SceneTextureKind::GoldMine2,
        SceneTextureKind::GoldMine3,
    };
    const int variant_index =
        (grid_x * 7 + grid_y * 13) % constants::GOLD_MINE_VARIANT_COUNT;
    const SceneTextureKind kind = variants[variant_index];
    if (scene_textures_.texture_id(kind) != 0U) {
        return kind;
    }

    for (const SceneTextureKind fallback : variants) {
        if (scene_textures_.texture_id(fallback) != 0U) {
            return fallback;
        }
    }

    return SceneTextureKind::GoldMine0;
}

void GameRenderer::draw_screen_sprite(
    const float screen_left,
    const float screen_top,
    const float screen_width,
    const float screen_height,
    const float depth,
    const unsigned int texture_id,
    const float tint_r,
    const float tint_g,
    const float tint_b,
    const float tint_a,
    const float uv_left,
    const float uv_right) const
{
    if (texture_id == 0U || window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    const auto to_clip_x = [window_width](const float screen_x) {
        return (screen_x / window_width) * 2.0F - 1.0F;
    };
    const auto to_clip_y = [window_height](const float screen_y) {
        return 1.0F - (screen_y / window_height) * 2.0F;
    };

    const float left = to_clip_x(screen_left);
    const float right = to_clip_x(screen_left + screen_width);
    const float top = to_clip_y(screen_top);
    const float bottom = to_clip_y(screen_top + screen_height);

    const std::array<TexturedSceneVertex, 6> vertices = {{
        {left, top, depth, uv_left, 1.0F, tint_r, tint_g, tint_b, tint_a},
        {right, top, depth, uv_right, 1.0F, tint_r, tint_g, tint_b, tint_a},
        {right, bottom, depth, uv_right, 0.0F, tint_r, tint_g, tint_b, tint_a},
        {left, top, depth, uv_left, 1.0F, tint_r, tint_g, tint_b, tint_a},
        {right, bottom, depth, uv_right, 0.0F, tint_r, tint_g, tint_b, tint_a},
        {left, bottom, depth, uv_left, 0.0F, tint_r, tint_g, tint_b, tint_a},
    }};

    append_textured_vertices(vertices.data(), vertices.size());
}

void GameRenderer::draw_textured_quad(
    const sf::Vector2f& top,
    const sf::Vector2f& right,
    const sf::Vector2f& bottom,
    const sf::Vector2f& left,
    const float uv_top_u,
    const float uv_top_v,
    const float uv_right_u,
    const float uv_right_v,
    const float uv_bottom_u,
    const float uv_bottom_v,
    const float uv_left_u,
    const float uv_left_v,
    const float depth,
    const float brightness_top,
    const float brightness_right,
    const float brightness_bottom,
    const float brightness_left,
    const float alpha) const
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);

    const auto to_clip = [&](const sf::Vector2f& screen_pos) {
        return std::array<float, 2>{
            (screen_pos.x / window_width) * 2.0F - 1.0F,
            1.0F - (screen_pos.y / window_height) * 2.0F,
        };
    };

    const auto top_clip = to_clip(top);
    const auto right_clip = to_clip(right);
    const auto bottom_clip = to_clip(bottom);
    const auto left_clip = to_clip(left);

    const std::array<TexturedSceneVertex, 6> vertices = {{
        {top_clip[0], top_clip[1], depth, uv_top_u, uv_top_v, brightness_top, brightness_top, brightness_top, alpha},
        {right_clip[0], right_clip[1], depth, uv_right_u, uv_right_v, brightness_right, brightness_right, brightness_right, alpha},
        {bottom_clip[0], bottom_clip[1], depth, uv_bottom_u, uv_bottom_v, brightness_bottom, brightness_bottom, brightness_bottom, alpha},
        {top_clip[0], top_clip[1], depth, uv_top_u, uv_top_v, brightness_top, brightness_top, brightness_top, alpha},
        {bottom_clip[0], bottom_clip[1], depth, uv_bottom_u, uv_bottom_v, brightness_bottom, brightness_bottom, brightness_bottom, alpha},
        {left_clip[0], left_clip[1], depth, uv_left_u, uv_left_v, brightness_left, brightness_left, brightness_left, alpha},
    }};

    append_textured_vertices(vertices.data(), vertices.size());
}

void GameRenderer::draw_textured_iso_diamond_fog(
    const sf::Vector2f& top,
    const sf::Vector2f& right,
    const sf::Vector2f& bottom,
    const sf::Vector2f& left,
    const float uv_top_u,
    const float uv_top_v,
    const float uv_right_u,
    const float uv_right_v,
    const float uv_bottom_u,
    const float uv_bottom_v,
    const float uv_left_u,
    const float uv_left_v,
    const float depth,
    const float self_brightness,
    const float corner_top,
    const float corner_right,
    const float corner_bottom,
    const float corner_left,
    const bool use_corner_fade,
    const float alpha_top,
    const float alpha_right,
    const float alpha_bottom,
    const float alpha_left) const
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    const bool alphas_vary = alpha_top != alpha_right || alpha_right != alpha_bottom
        || alpha_bottom != alpha_left;
    const bool brightness_varies = corner_top != self_brightness
        || corner_right != self_brightness
        || corner_bottom != self_brightness
        || corner_left != self_brightness;
    const bool needs_center_fan = alphas_vary || (use_corner_fade && brightness_varies);

    if (!needs_center_fan) {
        draw_textured_quad(
            top,
            right,
            bottom,
            left,
            uv_top_u,
            uv_top_v,
            uv_right_u,
            uv_right_v,
            uv_bottom_u,
            uv_bottom_v,
            uv_left_u,
            uv_left_v,
            depth,
            self_brightness,
            self_brightness,
            self_brightness,
            self_brightness,
            alpha_top);
        return;
    }

    const float window_width = static_cast<float>(window_size_.x);
    const float window_height = static_cast<float>(window_size_.y);
    const float center_alpha = (alpha_top + alpha_right + alpha_bottom + alpha_left) * 0.25F;

    const auto to_clip = [&](const sf::Vector2f& screen_pos) {
        return std::array<float, 2>{
            (screen_pos.x / window_width) * 2.0F - 1.0F,
            1.0F - (screen_pos.y / window_height) * 2.0F,
        };
    };

    const auto make_vertex = [&](
        const sf::Vector2f& screen_pos,
        const float uv_u,
        const float uv_v,
        const float brightness,
        const float vertex_alpha) {
        const auto clip = to_clip(screen_pos);
        return TexturedSceneVertex{
            clip[0],
            clip[1],
            depth,
            uv_u,
            uv_v,
            brightness,
            brightness,
            brightness,
            vertex_alpha,
        };
    };

    const sf::Vector2f center{
        (top.x + right.x + bottom.x + left.x) * 0.25F,
        (top.y + right.y + bottom.y + left.y) * 0.25F,
    };
    const float uv_center_u = 0.5F;
    const float uv_center_v = (uv_top_v + uv_bottom_v) * 0.5F;

    const TexturedSceneVertex center_vertex =
        make_vertex(center, uv_center_u, uv_center_v, self_brightness, center_alpha);
    const TexturedSceneVertex top_vertex =
        make_vertex(top, uv_top_u, uv_top_v, corner_top, alpha_top);
    const TexturedSceneVertex right_vertex =
        make_vertex(right, uv_right_u, uv_right_v, corner_right, alpha_right);
    const TexturedSceneVertex bottom_vertex =
        make_vertex(bottom, uv_bottom_u, uv_bottom_v, corner_bottom, alpha_bottom);
    const TexturedSceneVertex left_vertex =
        make_vertex(left, uv_left_u, uv_left_v, corner_left, alpha_left);

    const std::array<TexturedSceneVertex, 12> vertices = {{
        center_vertex,
        top_vertex,
        right_vertex,
        center_vertex,
        right_vertex,
        bottom_vertex,
        center_vertex,
        bottom_vertex,
        left_vertex,
        center_vertex,
        left_vertex,
        top_vertex,
    }};

    append_textured_vertices(vertices.data(), vertices.size());
}

void GameRenderer::draw_iso_diamond_sprite(
    const int grid_x,
    const int grid_y,
    const SceneTextureKind texture_kind,
    const float brightness,
    const std::vector<float>* fog_vertex_brightness,
    const int map_width,
    const int map_height,
    const float alpha,
    const float* corner_alphas) const
{
    if (scene_textures_.texture_id(texture_kind) == 0U) {
        return;
    }

    const IsoTileScreenCorners corners = camera_.grid_iso_corners(grid_x, grid_y);
    const float uv_left = constants::RENDER_GRASS_UV_LEFT;
    const float uv_top = constants::RENDER_GRASS_UV_TOP;
    const float uv_right = constants::RENDER_GRASS_UV_RIGHT;
    const float uv_bottom = constants::RENDER_GRASS_UV_BOTTOM;
    const float mid_v = (uv_top + uv_bottom) * 0.5F;
    const auto image_v_to_gl = [](const float image_v_from_top) {
        return 1.0F - image_v_from_top;
    };
    const float uv_top_gl = image_v_to_gl(uv_top);
    const float uv_right_gl = image_v_to_gl(mid_v);
    const float uv_bottom_gl = image_v_to_gl(uv_bottom);
    const float uv_left_gl = image_v_to_gl(mid_v);

    const std::array<float, 3> depth = camera_.world_to_clip(
        static_cast<float>(grid_x) + 0.5F,
        constants::RENDER_GROUND_SPRITE_SORT_Y,
        static_cast<float>(grid_y) + 0.5F);

    float corner_top = brightness;
    float corner_right = brightness;
    float corner_bottom = brightness;
    float corner_left = brightness;
    float center_brightness = brightness;
    const bool use_corner_fade = fog_vertex_brightness != nullptr
        && map_width > 0
        && map_height > 0;
    if (use_corner_fade) {
        const FogTileCornerBrightness corner_brightness = fog_tile_corner_brightness_from_vertices(
            *fog_vertex_brightness,
            map_width,
            map_height,
            grid_x,
            grid_y);
        corner_top = corner_brightness.top;
        corner_right = corner_brightness.right;
        corner_bottom = corner_brightness.bottom;
        corner_left = corner_brightness.left;
        center_brightness =
            (corner_top + corner_right + corner_bottom + corner_left) * 0.25F;
    }

    float alpha_top = alpha;
    float alpha_right = alpha;
    float alpha_bottom = alpha;
    float alpha_left = alpha;
    if (corner_alphas != nullptr) {
        alpha_top = corner_alphas[0];
        alpha_right = corner_alphas[1];
        alpha_bottom = corner_alphas[2];
        alpha_left = corner_alphas[3];
    }
    const bool use_alpha_fade = alpha_top != alpha_right || alpha_right != alpha_bottom
        || alpha_bottom != alpha_left;

    draw_textured_iso_diamond_fog(
        corners.top,
        corners.right,
        corners.bottom,
        corners.left,
        0.5F,
        uv_top_gl,
        uv_right,
        uv_right_gl,
        0.5F,
        uv_bottom_gl,
        uv_left,
        uv_left_gl,
        depth[2],
        center_brightness,
        corner_top,
        corner_right,
        corner_bottom,
        corner_left,
        use_corner_fade || use_alpha_fade,
        alpha_top,
        alpha_right,
        alpha_bottom,
        alpha_left);
}

void GameRenderer::draw_iso_object_sprite(
    const int grid_x,
    const int grid_y,
    const SceneTextureKind texture_kind,
    const float width_scale,
    const float sort_y,
    const float brightness,
    const float offset_x_tiles,
    const float offset_y_tiles,
    const int frame_index,
    const int frame_count) const
{
    const unsigned int texture_id = scene_textures_.texture_id(texture_kind);
    if (texture_id == 0U) {
        return;
    }

    const int frames = std::max(1, frame_count);
    const int frame = std::clamp(frame_index, 0, frames - 1);
    const float world_x = static_cast<float>(grid_x) + 0.5F + offset_x_tiles;
    const float world_z = static_cast<float>(grid_y) + 0.5F + offset_y_tiles;
    const sf::Vector2f anchor = camera_.world_to_screen(world_x, 0.0F, world_z);
    const float tile_width = camera_.tile_width() * width_scale;
    const float sprite_height =
        tile_width * scene_textures_.aspect_ratio(texture_kind) * static_cast<float>(frames);
    const float screen_left = anchor.x - tile_width * 0.5F;
    const float screen_top = anchor.y - sprite_height;

    const std::array<float, 3> depth = camera_.world_to_clip(world_x, sort_y, world_z);
    const float frame_width = 1.0F / static_cast<float>(frames);
    const float uv_left = static_cast<float>(frame) * frame_width;

    draw_screen_sprite(
        screen_left,
        screen_top,
        tile_width,
        sprite_height,
        depth[2],
        texture_id,
        brightness,
        brightness,
        brightness,
        1.0F,
        uv_left,
        uv_left + frame_width);
}

void GameRenderer::queue_iso_object_sprite(
    const int grid_x,
    const int grid_y,
    const SceneTextureKind texture_kind,
    const float width_scale,
    const float brightness,
    const float sort_key,
    const float offset_x_tiles,
    const float offset_y_tiles,
    const int frame_index,
    const int frame_count) const
{
    if (scene_textures_.texture_id(texture_kind) == 0U) {
        return;
    }

    pending_iso_sprites_.push_back(
        PendingIsoSpriteDraw{
            sort_key,
            grid_x,
            grid_y,
            texture_kind,
            width_scale,
            brightness,
            offset_x_tiles,
            offset_y_tiles,
            frame_index,
            frame_count,
        });
}

void GameRenderer::queue_unit_draw(const PendingUnitDraw& draw) const
{
    pending_unit_draws_.push_back(draw);
}

void GameRenderer::flush_pending_depth_sorted_draws(unsigned int& active_textured_batch) const
{
    if (pending_iso_sprites_.empty() && pending_unit_draws_.empty()) {
        return;
    }

    enum class PendingDrawKind {
        IsoSprite,
        Unit,
    };

    struct CombinedDraw {
        float sort_key{0.0F};
        PendingDrawKind kind{PendingDrawKind::IsoSprite};
        std::size_t index{0U};
    };

    std::vector<CombinedDraw> combined_draws{};
    combined_draws.reserve(pending_iso_sprites_.size() + pending_unit_draws_.size());
    for (std::size_t index = 0U; index < pending_iso_sprites_.size(); ++index) {
        combined_draws.push_back(
            CombinedDraw{
                pending_iso_sprites_[index].sort_key,
                PendingDrawKind::IsoSprite,
                index,
            });
    }
    for (std::size_t index = 0U; index < pending_unit_draws_.size(); ++index) {
        combined_draws.push_back(
            CombinedDraw{
                pending_unit_draws_[index].sort_key,
                PendingDrawKind::Unit,
                index,
            });
    }

    std::sort(
        combined_draws.begin(),
        combined_draws.end(),
        [](const CombinedDraw& left, const CombinedDraw& right) {
            return left.sort_key < right.sort_key;
        });

    const auto flush_textured_batch_if_needed = [&](const unsigned int texture_id) {
        if (texture_id == 0U) {
            return;
        }

        if (active_textured_batch != 0U && active_textured_batch != texture_id) {
            flush_textured_scene_batch(active_textured_batch);
        }

        active_textured_batch = texture_id;
    };

    const auto finish_textured_batch = [&]() {
        if (active_textured_batch != 0U) {
            flush_textured_scene_batch(active_textured_batch);
            active_textured_batch = 0U;
        }
    };

    glDisable(GL_DEPTH_TEST);
    for (const CombinedDraw& draw : combined_draws) {
        if (draw.kind == PendingDrawKind::IsoSprite) {
            if (!scene_batch_.empty()) {
                flush_scene_batch();
            }

            const PendingIsoSpriteDraw& iso_draw = pending_iso_sprites_[draw.index];
            flush_textured_batch_if_needed(scene_textures_.texture_id(iso_draw.texture_kind));
            const bool is_building_sprite = is_textured_building_kind(iso_draw.texture_kind);
            draw_iso_object_sprite(
                iso_draw.grid_x,
                iso_draw.grid_y,
                iso_draw.texture_kind,
                iso_draw.width_scale,
                is_building_sprite ? constants::RENDER_BUILDING_SPRITE_SORT_Y
                                   : constants::RENDER_TREE_SPRITE_SORT_Y,
                iso_draw.brightness,
                iso_draw.offset_x_tiles,
                iso_draw.offset_y_tiles,
                iso_draw.frame_index,
                iso_draw.frame_count);
            continue;
        }

        finish_textured_batch();

        if (!scene_batch_.empty()) {
            flush_scene_batch();
        }

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        const PendingUnitDraw& unit_draw = pending_unit_draws_[draw.index];
        draw_entity_cylinder(
            unit_draw.world_x,
            unit_draw.world_z,
            constants::RENDER_UNIT_HEIGHT,
            constants::RENDER_UNIT_RADIUS,
            unit_draw.r,
            unit_draw.g,
            unit_draw.b);
        if (!scene_batch_.empty()) {
            flush_scene_batch();
        }
        glDisable(GL_DEPTH_TEST);
    }
    finish_textured_batch();
    glEnable(GL_DEPTH_TEST);

    pending_iso_sprites_.clear();
    pending_unit_draws_.clear();
}

std::vector<core::GridPos> GameRenderer::collect_unit_front_occluder_tiles(
    const float world_x,
    const float world_z,
    const sim::components::MapGrid& map,
    const std::vector<sim::components::BuildingFootprint>& building_footprints,
    const std::vector<core::GridPos>& building_anchors) const
{
    std::vector<core::GridPos> occluder_tiles{};
    const int unit_tile_x = static_cast<int>(std::floor(world_x));
    const int unit_tile_y = static_cast<int>(std::floor(world_z));
    const int unit_sort = unit_tile_x + unit_tile_y;
    const core::GridPos unit_cell{unit_tile_x, unit_tile_y};

    const auto add_tile = [&](const core::GridPos cell, const bool is_building) {
        if (cell.x + cell.y <= unit_sort) {
            return;
        }

        if (core::chebyshev_distance(cell, unit_cell) > constants::RENDER_OCCLUSION_PROBE_RADIUS) {
            return;
        }

        if (!unit_is_occluded_by_tile(world_x, world_z, cell.x, cell.y, is_building)) {
            return;
        }

        if (std::find(occluder_tiles.begin(), occluder_tiles.end(), cell) != occluder_tiles.end()) {
            return;
        }

        occluder_tiles.push_back(cell);
    };

    for (int offset_y = -constants::RENDER_OCCLUSION_PROBE_RADIUS;
         offset_y <= constants::RENDER_OCCLUSION_PROBE_RADIUS;
         ++offset_y) {
        for (int offset_x = -constants::RENDER_OCCLUSION_PROBE_RADIUS;
             offset_x <= constants::RENDER_OCCLUSION_PROBE_RADIUS;
             ++offset_x) {
            const core::GridPos cell{unit_tile_x + offset_x, unit_tile_y + offset_y};
            if (!core::is_inside_grid(cell, map.width, map.height)) {
                continue;
            }

            const int index = core::grid_index(cell, map.width);
            if (map.tiles[static_cast<std::size_t>(index)] != sim::components::TileType::Forest) {
                continue;
            }

            if (map.forest_wood[static_cast<std::size_t>(index)] <= 0) {
                continue;
            }

            add_tile(cell, false);
        }
    }

    for (std::size_t index = 0U; index < building_anchors.size(); ++index) {
        const sim::components::GridPosition anchor{building_anchors[index]};
        const sim::components::BuildingFootprint& footprint = building_footprints[index];
        const int front_sort = (anchor.cell.x + footprint.width - 1) + (anchor.cell.y + footprint.height - 1);
        if (front_sort <= unit_sort) {
            continue;
        }

        if (sim::components::chebyshev_distance_to_footprint(unit_cell, anchor, footprint)
            > constants::RENDER_OCCLUSION_PROBE_RADIUS) {
            continue;
        }

        for (int cell_y = anchor.cell.y; cell_y < anchor.cell.y + footprint.height; ++cell_y) {
            for (int cell_x = anchor.cell.x; cell_x < anchor.cell.x + footprint.width; ++cell_x) {
                add_tile(core::GridPos{cell_x, cell_y}, true);
            }
        }
    }

    return occluder_tiles;
}

void GameRenderer::apply_iso_tile_scissor(
    const int grid_x,
    const int grid_y,
    const float vertical_extent_scale,
    const SceneTextureKind aspect_kind) const
{
    if (window_size_.x == 0U || window_size_.y == 0U) {
        return;
    }

    const IsoTileScreenCorners corners = camera_.grid_iso_corners(grid_x, grid_y);
    const float tile_width = camera_.tile_width()
        * (aspect_kind == SceneTextureKind::TownCenterFriendly
            || aspect_kind == SceneTextureKind::TownCenterEnemy
            ? static_cast<float>(constants::TOWN_CENTER_FOOTPRINT_TILES)
            : constants::RENDER_TREE_SPRITE_WIDTH_SCALE);
    const float sprite_height = tile_width * scene_textures_.aspect_ratio(aspect_kind)
        * vertical_extent_scale;

    const float min_x = std::min(
        std::min(corners.top.x, corners.right.x),
        std::min(corners.bottom.x, corners.left.x));
    const float max_x = std::max(
        std::max(corners.top.x, corners.right.x),
        std::max(corners.bottom.x, corners.left.x));
    const float min_y = corners.top.y - sprite_height;
    const float max_y = std::max(
        std::max(corners.top.y, corners.right.y),
        std::max(corners.bottom.y, corners.left.y));

    const float window_height = static_cast<float>(window_size_.y);
    const int scissor_x = std::max(0, static_cast<int>(std::floor(min_x)));
    const int scissor_y = std::max(0, static_cast<int>(std::floor(window_height - max_y)));
    const int scissor_w = std::max(
        1,
        static_cast<int>(std::ceil(max_x - min_x)));
    const int scissor_h = std::max(
        1,
        static_cast<int>(std::ceil(max_y - min_y)));

    glEnable(GL_SCISSOR_TEST);
    glScissor(scissor_x, scissor_y, scissor_w, scissor_h);
}

void GameRenderer::clear_screen_scissor() const
{
    glDisable(GL_SCISSOR_TEST);
}

void GameRenderer::draw_unit_occlusion_silhouette(
    const float world_x,
    const float world_z,
    const float r,
    const float g,
    const float b,
    const std::vector<core::GridPos>& occluder_tiles,
    const std::vector<core::GridPos>& building_anchors,
    const std::vector<sim::components::BuildingFootprint>& building_footprints) const
{
    if (occluder_tiles.empty()) {
        return;
    }

    for (const core::GridPos& cell : occluder_tiles) {
        const bool is_building_cell = cell_covered_by_building_footprint(
            cell.x,
            cell.y,
            building_anchors,
            building_footprints);
        apply_iso_tile_scissor(
            cell.x,
            cell.y,
            is_building_cell
                ? constants::RENDER_BUILDING_OCCLUSION_SCISSOR_SCALE
                : constants::RENDER_TREE_OCCLUSION_SCISSOR_SCALE,
            is_building_cell ? SceneTextureKind::TownCenterFriendly : SceneTextureKind::OakForestLarge);
        draw_unit_projected_silhouette_outline_immediate(world_x, world_z, r, g, b);
    }

    clear_screen_scissor();
}

void GameRenderer::draw_scene_triangles(
    const std::array<SceneVertex, 6>& triangle_vertices,
    const int vertex_count) const
{
    append_scene_vertices(triangle_vertices.data(), static_cast<std::size_t>(vertex_count));
}

void GameRenderer::draw_scene_quad(
    const float wx0,
    const float wy0,
    const float wz0,
    const float wx1,
    const float wy1,
    const float wz1,
    const float wx2,
    const float wy2,
    const float wz2,
    const float wx3,
    const float wy3,
    const float wz3,
    const float r,
    const float g,
    const float b,
    const float light_factor) const
{
    const float lit_r = r * light_factor;
    const float lit_g = g * light_factor;
    const float lit_b = b * light_factor;

    const std::array<SceneVertex, 6> vertices = {
        make_scene_vertex(wx0, wy0, wz0, lit_r, lit_g, lit_b),
        make_scene_vertex(wx1, wy1, wz1, lit_r, lit_g, lit_b),
        make_scene_vertex(wx2, wy2, wz2, lit_r, lit_g, lit_b),
        make_scene_vertex(wx0, wy0, wz0, lit_r, lit_g, lit_b),
        make_scene_vertex(wx2, wy2, wz2, lit_r, lit_g, lit_b),
        make_scene_vertex(wx3, wy3, wz3, lit_r, lit_g, lit_b),
    };

    draw_scene_triangles(vertices, 6);
}

void GameRenderer::draw_extruded_tile(
    const int grid_x,
    const int grid_y,
    const float extrude_height,
    const float r,
    const float g,
    const float b) const
{
    const float gx = static_cast<float>(grid_x);
    const float gy = static_cast<float>(grid_y);
    const float top = extrude_height;
    const float top_light = constants::RENDER_AMBIENT_LIGHT;
    const float side_light = constants::RENDER_AMBIENT_LIGHT * constants::RENDER_SIDE_LIGHT_FACTOR;

    draw_scene_quad(
        gx,
        top,
        gy,
        gx + 1.0F,
        top,
        gy,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        top_light);

    if (extrude_height <= 0.0F) {
        return;
    }

    draw_scene_quad(gx, 0.0F, gy, gx + 1.0F, 0.0F, gy, gx + 1.0F, top, gy, gx, top, gy, r, g, b, side_light);
    draw_scene_quad(
        gx + 1.0F,
        0.0F,
        gy,
        gx + 1.0F,
        0.0F,
        gy + 1.0F,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx + 1.0F,
        top,
        gy,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        gx,
        0.0F,
        gy + 1.0F,
        gx + 1.0F,
        0.0F,
        gy + 1.0F,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(gx, 0.0F, gy, gx, 0.0F, gy + 1.0F, gx, top, gy + 1.0F, gx, top, gy, r, g, b, side_light);
}

void GameRenderer::draw_tile_grid_lines(
    const int grid_x,
    const int grid_y,
    const float extrude_height,
    const float line_lift) const
{
    const float gx = static_cast<float>(grid_x);
    const float gy = static_cast<float>(grid_y);
    const float top = extrude_height + line_lift;
    const float width = constants::RENDER_GRID_LINE_WIDTH;
    const float r = constants::RENDER_GRID_LINE_R;
    const float g = constants::RENDER_GRID_LINE_G;
    const float b = constants::RENDER_GRID_LINE_B;

    draw_scene_quad(gx, top, gy, gx + 1.0F, top, gy, gx + 1.0F, top, gy + width, gx, top, gy + width, r, g, b, 1.0F);
    draw_scene_quad(
        gx,
        top,
        gy + 1.0F - width,
        gx + 1.0F,
        top,
        gy + 1.0F - width,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        1.0F);
    draw_scene_quad(gx, top, gy, gx, top, gy + 1.0F, gx + width, top, gy + 1.0F, gx + width, top, gy, r, g, b, 1.0F);
    draw_scene_quad(
        gx + 1.0F - width,
        top,
        gy,
        gx + 1.0F,
        top,
        gy,
        gx + 1.0F,
        top,
        gy + 1.0F,
        gx + 1.0F - width,
        top,
        gy + 1.0F,
        r,
        g,
        b,
        1.0F);
}

void GameRenderer::draw_iso_tile_grid_lines(const int grid_x, const int grid_y) const
{
    const IsoTileScreenCorners corners = camera_.grid_iso_corners(grid_x, grid_y);
    const float r = constants::RENDER_GRID_LINE_R;
    const float g = constants::RENDER_GRID_LINE_G;
    const float b = constants::RENDER_GRID_LINE_B;

    draw_screen_line_immediate(corners.top.x, corners.top.y, corners.right.x, corners.right.y, r, g, b);
    draw_screen_line_immediate(corners.right.x, corners.right.y, corners.bottom.x, corners.bottom.y, r, g, b);
    draw_screen_line_immediate(corners.bottom.x, corners.bottom.y, corners.left.x, corners.left.y, r, g, b);
    draw_screen_line_immediate(corners.left.x, corners.left.y, corners.top.x, corners.top.y, r, g, b);
}

void GameRenderer::draw_entity_prism(
    const float world_x,
    const float world_z,
    const float height,
    const float r,
    const float g,
    const float b) const
{
    const float inset = 0.18F;
    const float fx = world_x - 0.5F + inset;
    const float fz = world_z - 0.5F + inset;
    const float size = 1.0F - inset * 2.0F;
    const float base = constants::RENDER_ENTITY_BASE_LIFT;
    const float top = base + height;
    const float top_light = constants::RENDER_AMBIENT_LIGHT;
    const float side_light = constants::RENDER_AMBIENT_LIGHT * constants::RENDER_SIDE_LIGHT_FACTOR;

    draw_scene_quad(
        fx,
        top,
        fz,
        fx + size,
        top,
        fz,
        fx + size,
        top,
        fz + size,
        fx,
        top,
        fz + size,
        r,
        g,
        b,
        top_light);
    draw_scene_quad(
        fx,
        base,
        fz,
        fx + size,
        base,
        fz,
        fx + size,
        top,
        fz,
        fx,
        top,
        fz,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx + size,
        base,
        fz,
        fx + size,
        base,
        fz + size,
        fx + size,
        top,
        fz + size,
        fx + size,
        top,
        fz,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx,
        base,
        fz + size,
        fx + size,
        base,
        fz + size,
        fx + size,
        top,
        fz + size,
        fx,
        top,
        fz + size,
        r,
        g,
        b,
        side_light);
    draw_scene_quad(
        fx,
        base,
        fz,
        fx,
        base,
        fz + size,
        fx,
        top,
        fz + size,
        fx,
        top,
        fz,
        r,
        g,
        b,
        side_light);
}

void GameRenderer::draw_entity_cylinder(
    const float world_x,
    const float world_z,
    const float height,
    const float radius,
    const float r,
    const float g,
    const float b,
    const float height_start,
    const float height_end) const
{
    const float base = constants::RENDER_ENTITY_BASE_LIFT;
    const float clamped_start = std::clamp(height_start, 0.0F, 1.0F);
    const float clamped_end = std::clamp(height_end, clamped_start, 1.0F);
    const float body_base = base + height * clamped_start;
    const float top = base + height * clamped_end;
    const float top_light = constants::RENDER_AMBIENT_LIGHT;
    const float side_light = constants::RENDER_AMBIENT_LIGHT * constants::RENDER_SIDE_LIGHT_FACTOR;
    const float lit_top_r = r * top_light;
    const float lit_top_g = g * top_light;
    const float lit_top_b = b * top_light;
    const float lit_side_r = r * side_light;
    const float lit_side_g = g * side_light;
    const float lit_side_b = b * side_light;

    constexpr float k_two_pi = 6.2831853F;
    const int segments = constants::RENDER_CYLINDER_SEGMENTS;
    const float angle_step = k_two_pi / static_cast<float>(segments);

    for (int segment_index = 0; segment_index < segments; ++segment_index) {
        const float angle_a = angle_step * static_cast<float>(segment_index);
        const float angle_b = angle_step * static_cast<float>(segment_index + 1);
        const float ax = world_x + std::cos(angle_a) * radius;
        const float az = world_z + std::sin(angle_a) * radius;
        const float bx = world_x + std::cos(angle_b) * radius;
        const float bz = world_z + std::sin(angle_b) * radius;

        const std::array<SceneVertex, 3> top_triangle = {
            make_scene_vertex(world_x, top, world_z, lit_top_r, lit_top_g, lit_top_b),
            make_scene_vertex(ax, top, az, lit_top_r, lit_top_g, lit_top_b),
            make_scene_vertex(bx, top, bz, lit_top_r, lit_top_g, lit_top_b),
        };
        append_scene_vertices(top_triangle.data(), top_triangle.size());

        const std::array<SceneVertex, 6> side_quad = {
            make_scene_vertex(ax, body_base, az, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(bx, body_base, bz, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(bx, top, bz, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(ax, body_base, az, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(bx, top, bz, lit_side_r, lit_side_g, lit_side_b),
            make_scene_vertex(ax, top, az, lit_side_r, lit_side_g, lit_side_b),
        };
        append_scene_vertices(side_quad.data(), side_quad.size());
    }
}

void GameRenderer::draw_ground_highlight(const SceneHighlight& highlight) const
{
    const float half = 0.5F * highlight.scale;
    const float outline_height = constants::RENDER_GROUND_HIGHLIGHT_LIFT;
    const float line_width = constants::RENDER_HIGHLIGHT_LINE_WIDTH_TILES;

    draw_scene_rect_outline(
        highlight.world_x - half,
        highlight.world_z - half,
        highlight.world_x + half,
        highlight.world_z + half,
        outline_height,
        line_width,
        highlight.r,
        highlight.g,
        highlight.b);
}

void GameRenderer::draw_unit_ground_highlight(const SceneHighlight& highlight) const
{
    const float radius = constants::RENDER_UNIT_RADIUS * highlight.scale;
    const float outline_height = constants::RENDER_GROUND_HIGHLIGHT_LIFT;
    const float line_width = constants::RENDER_HIGHLIGHT_LINE_WIDTH_TILES;

    constexpr float k_two_pi = 6.2831853F;
    const int segments = constants::RENDER_CYLINDER_SEGMENTS;
    const float angle_step = k_two_pi / static_cast<float>(segments);

    for (int segment_index = 0; segment_index < segments; ++segment_index) {
        const float angle_a = angle_step * static_cast<float>(segment_index);
        const float angle_b = angle_step * static_cast<float>(segment_index + 1);
        const float ax = highlight.world_x + std::cos(angle_a) * radius;
        const float az = highlight.world_z + std::sin(angle_a) * radius;
        const float bx = highlight.world_x + std::cos(angle_b) * radius;
        const float bz = highlight.world_z + std::sin(angle_b) * radius;

        draw_scene_line_xz(
            ax,
            az,
            bx,
            bz,
            outline_height,
            highlight.r,
            highlight.g,
            highlight.b,
            line_width);
    }
}

void GameRenderer::draw_hitbox_overlays(
    const sim::Simulation& simulation,
    const float interpolation_alpha) const
{
    if (!show_hitboxes_) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    const auto& registry = simulation.registry();
    const float move_scale =
        constants::MOVE_UNIT_COLLISION_RADIUS_TILES / constants::RENDER_UNIT_RADIUS;
    const float melee_scale =
        constants::MELEE_UNIT_COLLISION_RADIUS_TILES / constants::RENDER_UNIT_RADIUS;

    const auto unit_view = registry.view<sim::components::UnitTag, sim::components::Health>();
    for (const entt::entity entity : unit_view) {
        if (unit_view.get<sim::components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
        draw_unit_ground_highlight(SceneHighlight{
            world_x,
            world_z,
            constants::HITBOX_DEBUG_UNIT_MOVE_R,
            constants::HITBOX_DEBUG_UNIT_MOVE_G,
            constants::HITBOX_DEBUG_UNIT_MOVE_B,
            move_scale,
        });
        draw_unit_ground_highlight(SceneHighlight{
            world_x,
            world_z,
            constants::HITBOX_DEBUG_UNIT_MELEE_R,
            constants::HITBOX_DEBUG_UNIT_MELEE_G,
            constants::HITBOX_DEBUG_UNIT_MELEE_B,
            melee_scale,
        });
    }

    const auto building_view =
        registry.view<sim::components::BuildingTag, sim::components::GridPosition>();
    for (const entt::entity entity : building_view) {
        const auto& anchor = building_view.get<sim::components::GridPosition>(entity);
        sim::components::BuildingFootprint footprint{};
        if (registry.any_of<sim::components::BuildingFootprint>(entity)) {
            footprint = registry.get<sim::components::BuildingFootprint>(entity);
        }
        footprint = sim::components::effective_building_footprint(
            footprint,
            registry.any_of<sim::components::TownCenterTag>(entity));
        draw_footprint_highlight(
            anchor.cell.x,
            anchor.cell.y,
            footprint.width,
            footprint.height,
            constants::HITBOX_DEBUG_BUILDING_R,
            constants::HITBOX_DEBUG_BUILDING_G,
            constants::HITBOX_DEBUG_BUILDING_B,
            1.0F);
    }

    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        return;
    }

    const auto& map = world_view.get<sim::components::MapGrid>(*world_view.begin());
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const int index = core::grid_index({x, y}, map.width);
            const auto tile = map.tiles[static_cast<std::size_t>(index)];
            bool blocked = false;
            if (tile == sim::components::TileType::Forest) {
                blocked = map.forest_wood[static_cast<std::size_t>(index)] > 0;
            }
            else if (tile == sim::components::TileType::Berries
                || tile == sim::components::TileType::Blueberries) {
                blocked = map.bush_food[static_cast<std::size_t>(index)] > 0;
            }
            else if (tile == sim::components::TileType::GoldMine) {
                blocked = static_cast<std::size_t>(index) < map.mine_money.size()
                    && map.mine_money[static_cast<std::size_t>(index)] > 0;
            }

            if (!blocked) {
                continue;
            }

            draw_ground_highlight(SceneHighlight{
                static_cast<float>(x) + 0.5F,
                static_cast<float>(y) + 0.5F,
                constants::HITBOX_DEBUG_RESOURCE_R,
                constants::HITBOX_DEBUG_RESOURCE_G,
                constants::HITBOX_DEBUG_RESOURCE_B,
                1.0F,
            });
        }
    }

    flush_scene_batch();
    glEnable(GL_DEPTH_TEST);
}

void GameRenderer::draw_hitbox_overlays_snapshot(
    const SimRenderSnapshot& snapshot,
    const float interpolation_alpha) const
{
    if (!show_hitboxes_) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    const float move_scale =
        constants::MOVE_UNIT_COLLISION_RADIUS_TILES / constants::RENDER_UNIT_RADIUS;
    const float melee_scale =
        constants::MELEE_UNIT_COLLISION_RADIUS_TILES / constants::RENDER_UNIT_RADIUS;

    for (const RenderEntityPose& pose : snapshot.units) {
        const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
        draw_unit_ground_highlight(SceneHighlight{
            world_x,
            world_z,
            constants::HITBOX_DEBUG_UNIT_MOVE_R,
            constants::HITBOX_DEBUG_UNIT_MOVE_G,
            constants::HITBOX_DEBUG_UNIT_MOVE_B,
            move_scale,
        });
        draw_unit_ground_highlight(SceneHighlight{
            world_x,
            world_z,
            constants::HITBOX_DEBUG_UNIT_MELEE_R,
            constants::HITBOX_DEBUG_UNIT_MELEE_G,
            constants::HITBOX_DEBUG_UNIT_MELEE_B,
            melee_scale,
        });
    }

    for (const RenderEntityPose& pose : snapshot.buildings) {
        draw_footprint_highlight(
            pose.grid_x,
            pose.grid_y,
            pose.footprint_width,
            pose.footprint_height,
            constants::HITBOX_DEBUG_BUILDING_R,
            constants::HITBOX_DEBUG_BUILDING_G,
            constants::HITBOX_DEBUG_BUILDING_B,
            1.0F);
    }

    for (int y = 0; y < snapshot.map_height; ++y) {
        for (int x = 0; x < snapshot.map_width; ++x) {
            const int index = core::grid_index({x, y}, snapshot.map_width);
            if (index < 0 || index >= static_cast<int>(snapshot.tiles.size())) {
                continue;
            }

            const auto tile = snapshot.tiles[static_cast<std::size_t>(index)];
            bool blocked = false;
            if (tile == sim::components::TileType::Forest) {
                blocked = index < static_cast<int>(snapshot.forest_wood.size())
                    && snapshot.forest_wood[static_cast<std::size_t>(index)] > 0;
            }
            else if (tile == sim::components::TileType::Berries
                || tile == sim::components::TileType::Blueberries) {
                blocked = index < static_cast<int>(snapshot.bush_food.size())
                    && snapshot.bush_food[static_cast<std::size_t>(index)] > 0;
            }
            else if (tile == sim::components::TileType::GoldMine) {
                blocked = index < static_cast<int>(snapshot.mine_money.size())
                    && snapshot.mine_money[static_cast<std::size_t>(index)] > 0;
            }

            if (!blocked) {
                continue;
            }

            draw_ground_highlight(SceneHighlight{
                static_cast<float>(x) + 0.5F,
                static_cast<float>(y) + 0.5F,
                constants::HITBOX_DEBUG_RESOURCE_R,
                constants::HITBOX_DEBUG_RESOURCE_G,
                constants::HITBOX_DEBUG_RESOURCE_B,
                1.0F,
            });
        }
    }

    flush_scene_batch();
    glEnable(GL_DEPTH_TEST);
}

void GameRenderer::draw_debug_path_for_unit(
    const float world_x,
    const float world_z,
    const std::vector<core::GridPos>& cells,
    int next_index) const
{
    if (next_index < 0) {
        next_index = 0;
    }

    const int cell_count = static_cast<int>(cells.size());
    if (next_index >= cell_count) {
        return;
    }

    sf::Vector2f previous = world_to_screen(
        world_x,
        constants::RENDER_ENTITY_BASE_LIFT,
        world_z);

    for (int i = next_index; i < cell_count; ++i) {
        const core::GridPos cell = cells[static_cast<std::size_t>(i)];
        const sf::Vector2f waypoint = tile_center_screen(
            cell.x,
            cell.y,
            constants::RENDER_ENTITY_BASE_LIFT);
        draw_screen_line_immediate(
            previous.x,
            previous.y,
            waypoint.x,
            waypoint.y,
            constants::RENDER_DEBUG_PATH_R,
            constants::RENDER_DEBUG_PATH_G,
            constants::RENDER_DEBUG_PATH_B);

        const IsoTileScreenCorners corners = camera_.grid_iso_corners(cell.x, cell.y);
        draw_screen_line_immediate(
            corners.top.x,
            corners.top.y,
            corners.right.x,
            corners.right.y,
            constants::RENDER_DEBUG_PATH_WAYPOINT_R,
            constants::RENDER_DEBUG_PATH_WAYPOINT_G,
            constants::RENDER_DEBUG_PATH_WAYPOINT_B);
        draw_screen_line_immediate(
            corners.right.x,
            corners.right.y,
            corners.bottom.x,
            corners.bottom.y,
            constants::RENDER_DEBUG_PATH_WAYPOINT_R,
            constants::RENDER_DEBUG_PATH_WAYPOINT_G,
            constants::RENDER_DEBUG_PATH_WAYPOINT_B);
        draw_screen_line_immediate(
            corners.bottom.x,
            corners.bottom.y,
            corners.left.x,
            corners.left.y,
            constants::RENDER_DEBUG_PATH_WAYPOINT_R,
            constants::RENDER_DEBUG_PATH_WAYPOINT_G,
            constants::RENDER_DEBUG_PATH_WAYPOINT_B);
        draw_screen_line_immediate(
            corners.left.x,
            corners.left.y,
            corners.top.x,
            corners.top.y,
            constants::RENDER_DEBUG_PATH_WAYPOINT_R,
            constants::RENDER_DEBUG_PATH_WAYPOINT_G,
            constants::RENDER_DEBUG_PATH_WAYPOINT_B);

        previous = waypoint;
    }
}

void GameRenderer::draw_debug_path_overlays(
    const sim::Simulation& simulation,
    const float interpolation_alpha) const
{
    if (!show_selection_debug_) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    const auto& registry = simulation.registry();
    const auto unit_view = registry.view<
        sim::components::UnitTag,
        sim::components::MovePath,
        sim::components::Health>();
    for (const entt::entity entity : unit_view) {
        if (unit_view.get<sim::components::Health>(entity).current.raw() <= 0) {
            continue;
        }

        const auto& path = unit_view.get<sim::components::MovePath>(entity);
        const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
        draw_debug_path_for_unit(world_x, world_z, path.cells, path.next_index);
    }

    glEnable(GL_DEPTH_TEST);
}

void GameRenderer::draw_debug_path_overlays_snapshot(
    const SimRenderSnapshot& snapshot,
    const float interpolation_alpha) const
{
    if (!show_selection_debug_) {
        return;
    }

    glDisable(GL_DEPTH_TEST);

    for (const RenderEntityPose& pose : snapshot.units) {
        if (pose.debug_path_cells.empty()) {
            continue;
        }

        const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
        draw_debug_path_for_unit(
            world_x,
            world_z,
            pose.debug_path_cells,
            pose.debug_path_next_index);
    }

    glEnable(GL_DEPTH_TEST);
}

void GameRenderer::draw_selection_outline(const float world_x, const float world_z) const
{
    draw_ground_highlight(
        SceneHighlight{
            world_x,
            world_z,
            constants::RENDER_SELECTION_HIGHLIGHT_R,
            constants::RENDER_SELECTION_HIGHLIGHT_G,
            constants::RENDER_SELECTION_HIGHLIGHT_B,
            constants::RENDER_SELECTION_OUTLINE_SCALE,
        });
}

void GameRenderer::draw_unit_selection_outline(const float world_x, const float world_z) const
{
    draw_unit_ground_highlight(
        SceneHighlight{
            world_x,
            world_z,
            constants::RENDER_SELECTION_HIGHLIGHT_R,
            constants::RENDER_SELECTION_HIGHLIGHT_G,
            constants::RENDER_SELECTION_HIGHLIGHT_B,
            constants::RENDER_SELECTION_OUTLINE_SCALE,
        });
}

void GameRenderer::draw_footprint_highlight(
    const int anchor_x,
    const int anchor_y,
    const int width,
    const int height,
    const float r,
    const float g,
    const float b,
    const float scale) const
{
    const float expand = (scale - 1.0F) * 0.5F;
    const float outline_height = constants::RENDER_GROUND_HIGHLIGHT_LIFT;
    const float line_width = constants::RENDER_HIGHLIGHT_LINE_WIDTH_TILES;

    draw_scene_rect_outline(
        static_cast<float>(anchor_x) - expand,
        static_cast<float>(anchor_y) - expand,
        static_cast<float>(anchor_x + width) + expand,
        static_cast<float>(anchor_y + height) + expand,
        outline_height,
        line_width,
        r,
        g,
        b);
}

void GameRenderer::draw_footprint_selection_outline(
    const int anchor_x,
    const int anchor_y,
    const int width,
    const int height) const
{
    draw_footprint_highlight(
        anchor_x,
        anchor_y,
        width,
        height,
        constants::RENDER_SELECTION_HIGHLIGHT_R,
        constants::RENDER_SELECTION_HIGHLIGHT_G,
        constants::RENDER_SELECTION_HIGHLIGHT_B,
        constants::RENDER_SELECTION_OUTLINE_SCALE);
}

float GameRenderer::unit_depth_sort_key(const float world_x, const float world_z) const
{
    return static_cast<float>(
        static_cast<int>(std::floor(world_x)) + static_cast<int>(std::floor(world_z)))
        + constants::RENDER_UNIT_SORT_BIAS;
}

float GameRenderer::cap_unit_sort_below_buildings(
    const float sort_key,
    const int unit_tile_x,
    const int unit_tile_y,
    const std::vector<core::GridPos>& building_anchors,
    const std::vector<sim::components::BuildingFootprint>& building_footprints) const
{
    float capped_sort = sort_key;
    const core::GridPos unit_cell{unit_tile_x, unit_tile_y};

    for (std::size_t index = 0U; index < building_anchors.size(); ++index) {
        const sim::components::GridPosition anchor{building_anchors[index]};
        const sim::components::BuildingFootprint& footprint = building_footprints[index];
        if (!sim::components::building_contains_cell(anchor, footprint, unit_cell)) {
            continue;
        }

        const float building_sort = static_cast<float>(
            (anchor.cell.x + footprint.width - 1) + (anchor.cell.y + footprint.height - 1))
            + constants::RENDER_BUILDING_SORT_BIAS;
        capped_sort = std::min(capped_sort, building_sort - 0.01F);
    }

    return capped_sort;
}

void GameRenderer::draw_interaction_highlights(
    const entt::registry* registry,
    const SimRenderSnapshot* snapshot,
    const std::vector<entt::entity>& selected_entities,
    const entt::entity hover_unit,
    const bool hover_unit_is_enemy,
    const entt::entity hover_building,
    const std::optional<core::GridPos> hover_resource_cell,
    const std::optional<core::GridPos> selected_resource_cell,
    const entt::entity selected_building,
    const float interpolation_alpha) const
{
    const auto find_building_pose = [&](const entt::entity entity) -> const RenderEntityPose* {
        if (snapshot == nullptr) {
            return nullptr;
        }

        for (const RenderEntityPose& pose : snapshot->buildings) {
            if (pose.entity == entity) {
                return &pose;
            }
        }

        return nullptr;
    };

    glDisable(GL_DEPTH_TEST);

    if (registry != nullptr) {
        for (const entt::entity entity : selected_entities) {
            if (!registry->valid(entity) || !registry->any_of<sim::components::GridPosition>(entity)) {
                continue;
            }

            if (registry->any_of<sim::components::UnitTag>(entity)) {
                const auto [world_x, world_z] = unit_render_world_xz(*registry, entity, interpolation_alpha);
                draw_unit_selection_outline(world_x, world_z);
                continue;
            }

            if (registry->any_of<sim::components::BuildingTag>(entity)) {
                const auto& anchor = registry->get<sim::components::GridPosition>(entity);
                const sim::components::BuildingFootprint footprint =
                    resolve_registry_building_footprint(*registry, entity);
                draw_footprint_selection_outline(
                    anchor.cell.x,
                    anchor.cell.y,
                    footprint.width,
                    footprint.height);
            }
        }

        if (selected_resource_cell.has_value()) {
            draw_selection_outline(
                static_cast<float>(selected_resource_cell->x) + 0.5F,
                static_cast<float>(selected_resource_cell->y) + 0.5F);
        }

        if (selected_building != entt::null && registry->valid(selected_building)
            && registry->any_of<sim::components::GridPosition>(selected_building)) {
            const auto& anchor = registry->get<sim::components::GridPosition>(selected_building);
            const sim::components::BuildingFootprint footprint =
                resolve_registry_building_footprint(*registry, selected_building);
            draw_footprint_selection_outline(
                anchor.cell.x,
                anchor.cell.y,
                footprint.width,
                footprint.height);
        }

        const sim::components::FogOfWarState* fog = nullptr;
        if (fog_of_war_enabled_) {
            const auto world_view =
                registry->view<sim::components::WorldTag, sim::components::FogOfWarState>();
            if (world_view.begin() != world_view.end()) {
                fog = &world_view.get<sim::components::FogOfWarState>(*world_view.begin());
            }
        }

        const auto construction_view = registry->view<
            sim::components::BuildingTag,
            sim::components::UnderConstructionTag,
            sim::components::GridPosition,
            sim::components::Health>();
        for (const entt::entity entity : construction_view) {
            if (construction_view.get<sim::components::Health>(entity).current.raw() <= 0) {
                continue;
            }

            const bool visible = fog == nullptr
                || sim::systems::is_entity_visible_to_slot(
                       *registry, *fog, entity, local_player_slot_);
            if (!visible) {
                // Last-seen ghost only — never leak live construction in shroud.
                const RenderEntityPose* remembered = building_sight_memory_.find(entity);
                if (remembered == nullptr || !remembered->under_construction) {
                    continue;
                }
            }

            const auto& anchor = construction_view.get<sim::components::GridPosition>(entity);
            const sim::components::BuildingFootprint footprint =
                resolve_registry_building_footprint(*registry, entity);
            draw_footprint_highlight(
                anchor.cell.x,
                anchor.cell.y,
                footprint.width,
                footprint.height,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_R,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_G,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_B,
                constants::RENDER_SELECTION_OUTLINE_SCALE);
        }

        // Keep remembered under-construction ghosts after the live building finishes.
        for (const RenderEntityPose& remembered : building_sight_memory_.remembered_poses()) {
            if (!remembered.under_construction || remembered.health_current <= 0) {
                continue;
            }

            if (fog != nullptr
                && sim::systems::is_entity_visible_to_slot(
                       *registry, *fog, remembered.entity, local_player_slot_)) {
                continue;
            }

            if (registry->valid(remembered.entity)
                && registry->any_of<sim::components::UnderConstructionTag>(remembered.entity)) {
                continue;
            }

            draw_footprint_highlight(
                remembered.grid_x,
                remembered.grid_y,
                remembered.footprint_width,
                remembered.footprint_height,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_R,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_G,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_B,
                constants::RENDER_SELECTION_OUTLINE_SCALE);
        }

        if (hover_unit != entt::null && registry->valid(hover_unit)
            && registry->any_of<sim::components::GridPosition>(hover_unit)) {
            const auto [world_x, world_z] = unit_render_world_xz(*registry, hover_unit, interpolation_alpha);
            const bool is_selected = std::find(selected_entities.begin(), selected_entities.end(), hover_unit)
                != selected_entities.end();
            if (!is_selected) {
                draw_unit_ground_highlight(
                    SceneHighlight{
                        world_x,
                        world_z,
                        hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_R
                                            : constants::RENDER_HOVER_FRIENDLY_R,
                        hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_G
                                            : constants::RENDER_HOVER_FRIENDLY_G,
                        hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_B
                                            : constants::RENDER_HOVER_FRIENDLY_B,
                        constants::RENDER_HOVER_OUTLINE_SCALE,
                    });
            }
        }
        else if (hover_building != entt::null && registry->valid(hover_building)
            && registry->any_of<sim::components::GridPosition>(hover_building)
            && hover_building != selected_building) {
            const bool enemy_building = sim::components::is_opponent_entity(
                *registry, hover_building, local_player_slot_);
            const bool unseen_enemy = enemy_building && fog != nullptr
                && !sim::systems::is_entity_visible_to_slot(
                       *registry, *fog, hover_building, local_player_slot_)
                && building_sight_memory_.find(hover_building) == nullptr;
            if (!unseen_enemy) {
                const auto& anchor = registry->get<sim::components::GridPosition>(hover_building);
                const sim::components::BuildingFootprint footprint =
                    resolve_registry_building_footprint(*registry, hover_building);
                draw_footprint_highlight(
                    anchor.cell.x,
                    anchor.cell.y,
                    footprint.width,
                    footprint.height,
                    constants::RENDER_HOVER_FRIENDLY_R,
                    constants::RENDER_HOVER_FRIENDLY_G,
                    constants::RENDER_HOVER_FRIENDLY_B,
                    constants::RENDER_HOVER_OUTLINE_SCALE);
            }
        }
        else if (hover_resource_cell.has_value()
            && (!selected_resource_cell.has_value()
                || *hover_resource_cell != *selected_resource_cell)) {
            draw_ground_highlight(
                SceneHighlight{
                    static_cast<float>(hover_resource_cell->x) + 0.5F,
                    static_cast<float>(hover_resource_cell->y) + 0.5F,
                    constants::RENDER_HOVER_RESOURCE_R,
                    constants::RENDER_HOVER_RESOURCE_G,
                    constants::RENDER_HOVER_RESOURCE_B,
                    constants::RENDER_HOVER_OUTLINE_SCALE,
                });
        }
    }
    else if (snapshot != nullptr) {
        const auto find_unit_pose = [&](const entt::entity entity) -> const RenderEntityPose* {
            for (const RenderEntityPose& pose : snapshot->units) {
                if (pose.entity == entity) {
                    return &pose;
                }
            }

            return nullptr;
        };

        for (const entt::entity entity : selected_entities) {
            const RenderEntityPose* unit_pose = find_unit_pose(entity);
            if (unit_pose != nullptr) {
                const auto [world_x, world_z] = interpolate_render_pose(*unit_pose, interpolation_alpha);
                if (snapshot->fog_visible.empty()
                    || snapshot_cell_is_visible(
                        *snapshot,
                        snapshot_world_visibility_cell(world_x, world_z))) {
                    draw_unit_selection_outline(world_x, world_z);
                }
                continue;
            }

            const RenderEntityPose* building_pose = find_building_pose(entity);
            if (building_pose != nullptr) {
                const sim::components::BuildingFootprint footprint =
                    resolve_pose_building_footprint(*building_pose);
                draw_footprint_selection_outline(
                    building_pose->grid_x,
                    building_pose->grid_y,
                    footprint.width,
                    footprint.height);
            }
        }

        if (selected_resource_cell.has_value()) {
            draw_selection_outline(
                static_cast<float>(selected_resource_cell->x) + 0.5F,
                static_cast<float>(selected_resource_cell->y) + 0.5F);
        }

        if (selected_building != entt::null) {
            const RenderEntityPose* building_pose = find_building_pose(selected_building);
            if (building_pose != nullptr) {
                const sim::components::BuildingFootprint footprint =
                    resolve_pose_building_footprint(*building_pose);
                draw_footprint_selection_outline(
                    building_pose->grid_x,
                    building_pose->grid_y,
                    footprint.width,
                    footprint.height);
            }
        }

        for (const RenderEntityPose& pose : snapshot->buildings) {
            if (!pose.under_construction || pose.health_current <= 0) {
                continue;
            }

            const sim::components::BuildingFootprint footprint = resolve_pose_building_footprint(pose);
            draw_footprint_highlight(
                pose.grid_x,
                pose.grid_y,
                footprint.width,
                footprint.height,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_R,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_G,
                constants::RENDER_CONSTRUCTION_HIGHLIGHT_B,
                constants::RENDER_SELECTION_OUTLINE_SCALE);
        }

        if (hover_unit != entt::null) {
            const RenderEntityPose* pose = find_unit_pose(hover_unit);
            if (pose != nullptr) {
                const bool is_selected = std::find(selected_entities.begin(), selected_entities.end(), hover_unit)
                    != selected_entities.end();
                if (!is_selected) {
                    const auto [world_x, world_z] = interpolate_render_pose(*pose, interpolation_alpha);
                    const bool render_hover = snapshot->fog_visible.empty()
                        || snapshot_cell_is_visible(
                            *snapshot,
                            snapshot_world_visibility_cell(world_x, world_z));
                    if (render_hover) {
                        draw_unit_ground_highlight(
                            SceneHighlight{
                                world_x,
                                world_z,
                                hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_R
                                                    : constants::RENDER_HOVER_FRIENDLY_R,
                                hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_G
                                                    : constants::RENDER_HOVER_FRIENDLY_G,
                                hover_unit_is_enemy ? constants::RENDER_HOVER_ENEMY_B
                                                    : constants::RENDER_HOVER_FRIENDLY_B,
                                constants::RENDER_HOVER_OUTLINE_SCALE,
                            });
                    }
                }
            }
        }
        else if (hover_building != entt::null && hover_building != selected_building) {
            const RenderEntityPose* building_pose = find_building_pose(hover_building);
            if (building_pose != nullptr) {
                const sim::components::BuildingFootprint footprint =
                    resolve_pose_building_footprint(*building_pose);
                draw_footprint_highlight(
                    building_pose->grid_x,
                    building_pose->grid_y,
                    footprint.width,
                    footprint.height,
                    constants::RENDER_HOVER_FRIENDLY_R,
                    constants::RENDER_HOVER_FRIENDLY_G,
                    constants::RENDER_HOVER_FRIENDLY_B,
                    constants::RENDER_HOVER_OUTLINE_SCALE);
            }
        }
        else if (hover_resource_cell.has_value()
            && (!selected_resource_cell.has_value()
                || *hover_resource_cell != *selected_resource_cell)) {
            draw_ground_highlight(
                SceneHighlight{
                    static_cast<float>(hover_resource_cell->x) + 0.5F,
                    static_cast<float>(hover_resource_cell->y) + 0.5F,
                    constants::RENDER_HOVER_RESOURCE_R,
                    constants::RENDER_HOVER_RESOURCE_G,
                    constants::RENDER_HOVER_RESOURCE_B,
                    constants::RENDER_HOVER_OUTLINE_SCALE,
                });
        }
    }

    if (!scene_batch_.empty()) {
        flush_scene_batch();
    }

    glEnable(GL_DEPTH_TEST);
}

void GameRenderer::draw(
    const sim::Simulation& simulation,
    const std::vector<entt::entity>& selected_entities,
    const float interpolation_alpha,
    const SelectionBoxOverlay& selection_box,
    const entt::entity hover_unit,
    const bool hover_unit_is_enemy,
    const entt::entity hover_building,
    const std::optional<core::GridPos> hover_resource_cell,
    const std::optional<core::GridPos> selected_resource_cell,
    const entt::entity selected_building,
    const float fps,
    const float tps,
    const net::LockstepNetworkHudStats& network_stats,
    const HudUnitContext& hud_context_input,
    const std::optional<core::GridPos> placement_ghost_anchor,
    const bool placement_ghost_valid)
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    const bool offscreen = ensure_present_target();
    if (offscreen) {
        glBindFramebuffer(GL_FRAMEBUFFER, present_fbo_);
    }

    glDisable(GL_SCISSOR_TEST);
    if (window_size_.x > 0U && window_size_.y > 0U) {
        glViewport(0, 0, static_cast<GLsizei>(window_size_.x), static_cast<GLsizei>(window_size_.y));
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0U);
    clear_opaque_framebuffer(
        constants::RENDER_CLEAR_R,
        constants::RENDER_CLEAR_G,
        constants::RENDER_CLEAR_B,
        constants::RENDER_CLEAR_A);

    const auto& registry = simulation.registry();
    const auto world_view = registry.view<sim::components::WorldTag, sim::components::MapGrid>();
    if (world_view.begin() == world_view.end()) {
        if (offscreen) {
            present_target_to_window();
        }
        return;
    }

    const entt::entity world = *world_view.begin();
    const auto& map = world_view.get<sim::components::MapGrid>(world);
    const sim::components::FogOfWarState* fog = nullptr;
    if (fog_of_war_enabled_ && registry.any_of<sim::components::FogOfWarState>(world)) {
        fog = &registry.get<sim::components::FogOfWarState>(world);
    }

    if (!map_framed_) {
        try_frame_player_start(map.width, map.height, &simulation, nullptr);
    }

    const std::uint64_t hud_tick = simulation.tick_count();
    if (cached_hud_snapshot_tick_ != hud_tick) {
        cached_hud_snapshot_ = capture_sim_render_snapshot(
            registry, local_player_slot_, &building_sight_memory_, hud_tick);
        cached_hud_snapshot_tick_ = hud_tick;
    }
    const SimRenderSnapshot& hud_snapshot = cached_hud_snapshot_;

    scene_batch_.clear();
    textured_scene_batch_.clear();
    pending_iso_sprites_.clear();
    pending_unit_draws_.clear();

    std::vector<core::GridPos> building_anchors{};
    std::vector<sim::components::BuildingFootprint> building_footprints{};
    const auto is_shrouded_building = [&](const entt::entity entity, const bool visible) {
        if (visible || fog == nullptr) {
            return false;
        }

        return building_sight_memory_.find(entity) != nullptr
            && sim::systems::is_building_renderable_in_shroud(
                   registry, *fog, entity, local_player_slot_);
    };
    const auto remembered_under_construction = [&](const entt::entity entity) {
        const RenderEntityPose* remembered = building_sight_memory_.find(entity);
        return remembered != nullptr && remembered->under_construction;
    };
    const auto collect_building_footprints = [&]() {
        building_anchors.clear();
        building_footprints.clear();

        const auto footprint_view = registry.view<
            sim::components::BuildingTag,
            sim::components::GridPosition,
            sim::components::Health>();
        for (const entt::entity entity : footprint_view) {
            const auto& health = footprint_view.get<sim::components::Health>(entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const bool visible = fog == nullptr
                || sim::systems::is_entity_visible_to_slot(registry, *fog, entity, local_player_slot_);
            const bool shrouded = is_shrouded_building(entity, visible);
            if (!visible && !shrouded) {
                continue;
            }

            const auto& anchor = footprint_view.get<sim::components::GridPosition>(entity);
            const sim::components::BuildingFootprint footprint = resolve_registry_building_footprint(
                registry,
                entity);
            building_anchors.push_back(anchor.cell);
            building_footprints.push_back(footprint);
        }
    };

    const bool use_textured_tiles = scene_textures_.is_loaded();
    const std::vector<sim::systems::VisionSource> vision_sources = (fog != nullptr && use_textured_tiles)
        ? sim::systems::collect_vision_sources_for_slot(registry, local_player_slot_)
        : std::vector<sim::systems::VisionSource>{};
    const std::vector<float>& fog_vertex_brightness =
        (fog != nullptr && use_textured_tiles)
        ? ensure_fog_vertex_brightness(
            simulation.tick_count(),
            fog->explored,
            map.width,
            map.height,
            local_player_slot_,
            vision_sources)
        : fog_vertex_cache_;
    const std::vector<float>* fog_vertex_brightness_ptr =
        (fog != nullptr && use_textured_tiles && !fog_vertex_brightness.empty())
        ? &fog_vertex_brightness
        : nullptr;
    unsigned int active_textured_batch = 0U;

    const auto flush_textured_batch_if_needed = [&](const unsigned int texture_id) {
        if (texture_id == 0U) {
            return;
        }

        if (active_textured_batch != 0U && active_textured_batch != texture_id) {
            flush_textured_scene_batch(active_textured_batch);
        }

        active_textured_batch = texture_id;
    };

    const auto finish_textured_batches = [&]() {
        if (active_textured_batch != 0U) {
            flush_textured_scene_batch(active_textured_batch);
            active_textured_batch = 0U;
        }
    };

    if (use_textured_tiles) {
        const std::vector<std::uint8_t> empty_fog{};
        draw_batched_ground_tiles(
            map.width,
            map.height,
            map.ground,
            fog != nullptr ? fog->visible : empty_fog,
            fog != nullptr ? fog->explored : empty_fog,
            fog != nullptr,
            fog_vertex_brightness_ptr,
            simulation.tick_count());
    }
    else {
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const int index = y * map.width + x;
                const auto live_tile = map.tiles[static_cast<std::size_t>(index)];
                const int live_forest_wood = map.forest_wood[static_cast<std::size_t>(index)];
                const bool use_memory = fog != nullptr
                    && fog_tile_is_in_shroud(
                        fog->visible,
                        fog->explored,
                        map.width,
                        map.height,
                        local_player_slot_,
                        x,
                        y);

                sim::components::TileType tile = live_tile;
                int forest_wood = live_forest_wood;
                float r = 0.20F;
                float g = 0.32F;
                float b = 0.18F;
                float extrude = 0.0F;
                resolve_tile_appearance(
                    live_tile,
                    live_forest_wood,
                    use_memory,
                    use_memory
                        ? sim::components::fog_memory_tile_type(*fog, map, x, y, local_player_slot_)
                        : live_tile,
                    use_memory
                        ? sim::components::fog_memory_forest_wood(*fog, map, x, y, local_player_slot_)
                        : live_forest_wood,
                    tile,
                    forest_wood,
                    r,
                    g,
                    b,
                    extrude);

                float brightness = 1.0F;
                if (fog != nullptr) {
                    brightness = fog_tile_brightness(
                        fog->visible,
                        fog->explored,
                        map.width,
                        map.height,
                        local_player_slot_,
                        x,
                        y);
                }

                r *= brightness;
                g *= brightness;
                b *= brightness;

                draw_extruded_tile(x, y, extrude, r, g, b);
                if (show_grid_lines_ && (r > 0.0F || g > 0.0F || b > 0.0F)) {
                    draw_tile_grid_lines(x, y, extrude);
                }
            }
        }
    }

    finish_textured_batches();

    if (!scene_batch_.empty()) {
        flush_scene_batch();
    }

    draw_interaction_highlights(
        &registry,
        nullptr,
        selected_entities,
        hover_unit,
        hover_unit_is_enemy,
        hover_building,
        hover_resource_cell,
        selected_resource_cell,
        selected_building,
        interpolation_alpha);

    if (use_textured_tiles) {
        collect_building_footprints();

        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                if (!iso_tile_intersects_window(
                        x,
                        y,
                        constants::RENDER_OBJECT_SCREEN_CULL_PAD_TILES)) {
                    continue;
                }

                if (fog != nullptr
                    && is_fog_tile_unexplored(
                        fog->visible,
                        fog->explored,
                        map.width,
                        map.height,
                        local_player_slot_,
                        x,
                        y)) {
                    continue;
                }

                if (cell_covered_by_building_footprint(x, y, building_anchors, building_footprints)) {
                    continue;
                }

                const int index = y * map.width + x;
                const auto live_tile = map.tiles[static_cast<std::size_t>(index)];
                const int live_forest_wood = map.forest_wood[static_cast<std::size_t>(index)];
                const bool use_memory = fog != nullptr
                    && fog_tile_is_in_shroud(
                        fog->visible,
                        fog->explored,
                        map.width,
                        map.height,
                        local_player_slot_,
                        x,
                        y);

                sim::components::TileType tile = live_tile;
                int forest_wood = live_forest_wood;
                float unused_r = 0.0F;
                float unused_g = 0.0F;
                float unused_b = 0.0F;
                float unused_extrude = 0.0F;
                resolve_tile_appearance(
                    live_tile,
                    live_forest_wood,
                    use_memory,
                    use_memory
                        ? sim::components::fog_memory_tile_type(*fog, map, x, y, local_player_slot_)
                        : live_tile,
                    use_memory
                        ? sim::components::fog_memory_forest_wood(*fog, map, x, y, local_player_slot_)
                        : live_forest_wood,
                    tile,
                    forest_wood,
                    unused_r,
                    unused_g,
                    unused_b,
                    unused_extrude);

                const int live_bush_food = map.bush_food[static_cast<std::size_t>(index)];
                const int memory_bush_food = use_memory
                    ? sim::components::fog_memory_bush_food(*fog, map, x, y, local_player_slot_)
                    : live_bush_food;
                const int bush_food = use_memory ? memory_bush_food : live_bush_food;
                const int live_mine_money =
                    static_cast<std::size_t>(index) < map.mine_money.size()
                    ? map.mine_money[static_cast<std::size_t>(index)]
                    : 0;
                const int memory_mine_money = use_memory
                    ? sim::components::fog_memory_mine_money(*fog, map, x, y, local_player_slot_)
                    : live_mine_money;
                const int mine_money = use_memory ? memory_mine_money : live_mine_money;
                const bool draw_forest = tile == sim::components::TileType::Forest && forest_wood > 0;
                const bool draw_bush =
                    (tile == sim::components::TileType::Berries
                        || tile == sim::components::TileType::Blueberries)
                    && bush_food > 0;
                const bool draw_gold_mine =
                    tile == sim::components::TileType::GoldMine && mine_money > 0;
                if (!draw_forest && !draw_bush && !draw_gold_mine) {
                    continue;
                }

                float brightness = 1.0F;
                if (fog != nullptr) {
                    brightness = fog_tile_brightness(
                        fog->visible,
                        fog->explored,
                        map.width,
                        map.height,
                        local_player_slot_,
                        x,
                        y);
                }
                brightness = fog_object_brightness(
                    fog_vertex_brightness_ptr,
                    map.width,
                    map.height,
                    x,
                    y,
                    brightness);

                const SceneTextureKind object_kind = draw_forest
                    ? forest_tree_texture_for_cell(
                        x,
                        y,
                        sim::components::ground_at(map, static_cast<std::size_t>(index)))
                    : (draw_gold_mine
                        ? gold_mine_texture_for_cell(x, y)
                        : (tile == sim::components::TileType::Blueberries
                            ? SceneTextureKind::Blueberries
                            : SceneTextureKind::Berries));
                const float width_scale = draw_gold_mine
                    ? constants::RENDER_GOLD_MINE_SPRITE_WIDTH_SCALE
                    : (draw_forest
                        ? tree_sprite_width_scale(object_kind)
                        : constants::RENDER_TREE_SPRITE_WIDTH_SCALE);
                float offset_x = 0.0F;
                float offset_y = 0.0F;
                if (draw_gold_mine) {
                    offset_x = constants::RENDER_GOLD_MINE_SPRITE_OFFSET_X;
                    offset_y = constants::RENDER_GOLD_MINE_SPRITE_OFFSET_Y;
                }
                else if (draw_forest) {
                    const auto tree_offset = tree_sprite_offset_for_kind(object_kind);
                    offset_x = tree_offset.first;
                    offset_y = tree_offset.second;
                }
                else {
                    offset_x = constants::RENDER_BERRY_SPRITE_OFFSET_X;
                    offset_y = constants::RENDER_BERRY_SPRITE_OFFSET_Y;
                }
                queue_iso_object_sprite(
                    x,
                    y,
                    object_kind,
                    width_scale,
                    brightness,
                    static_cast<float>(x + y) + constants::RENDER_TREE_SORT_BIAS,
                    offset_x,
                    offset_y);
            }
        }

        const int mana_lake_frame = mana_lake_animation_frame();
        const auto mana_lake_view = registry.view<
            sim::components::ManaLakeTag,
            sim::components::GridPosition,
            sim::components::BuildingFootprint>();
        for (const entt::entity entity : mana_lake_view) {
            const entt::entity extractor_on_lake =
                sim::spawn::find_extractor_on_mana_lake(registry, entity);
            if (extractor_on_lake != entt::null
                && registry.valid(extractor_on_lake)
                && !registry.any_of<sim::components::UnderConstructionTag>(extractor_on_lake)
                && registry.any_of<sim::components::Health>(extractor_on_lake)
                && registry.get<sim::components::Health>(extractor_on_lake).current.raw() > 0) {
                // Completed extractor covers the lake (SC geyser style: lake returns on destroy).
                continue;
            }

            const auto& anchor = mana_lake_view.get<sim::components::GridPosition>(entity);
            const auto& footprint = mana_lake_view.get<sim::components::BuildingFootprint>(entity);
            float brightness = 1.0F;
            if (fog != nullptr) {
                if (!sim::systems::is_cell_explored_to_slot(*fog, anchor.cell, local_player_slot_)) {
                    continue;
                }

                if (!sim::systems::is_cell_visible_to_slot(*fog, anchor.cell, local_player_slot_)) {
                    brightness = constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
                }
            }

            queue_iso_object_sprite(
                anchor.cell.x + footprint.width / 2,
                anchor.cell.y + footprint.height / 2,
                SceneTextureKind::ManaLake,
                constants::RENDER_MANA_LAKE_SPRITE_WIDTH_SCALE,
                brightness,
                static_cast<float>(
                    (anchor.cell.x + footprint.width - 1) + (anchor.cell.y + footprint.height - 1))
                    + constants::RENDER_MANA_LAKE_SORT_BIAS,
                constants::RENDER_MANA_LAKE_SPRITE_OFFSET_X,
                constants::RENDER_MANA_LAKE_SPRITE_OFFSET_Y,
                mana_lake_frame,
                constants::MANA_LAKE_ANIMATION_FRAME_COUNT);
        }

        const auto building_view = registry.view<
            sim::components::BuildingTag,
            sim::components::GridPosition,
            sim::components::Health>();
        for (const entt::entity entity : building_view) {
            const bool visible = fog == nullptr
                || sim::systems::is_entity_visible_to_slot(registry, *fog, entity, local_player_slot_);
            const bool shrouded = is_shrouded_building(entity, visible);
            if (!visible && !shrouded) {
                continue;
            }

            const bool is_town_center = registry.any_of<sim::components::TownCenterTag>(entity);
            const bool is_house = registry.any_of<sim::components::HouseTag>(entity);
            const bool is_lumberjack = registry.any_of<sim::components::LumberjackTag>(entity);
            const bool is_extractor = registry.any_of<sim::components::ExtractorTag>(entity);
            if (!is_town_center && !is_house && !is_lumberjack && !is_extractor) {
                continue;
            }

            const bool under_construction = visible
                ? registry.any_of<sim::components::UnderConstructionTag>(entity)
                : remembered_under_construction(entity);
            if (under_construction) {
                continue;
            }

            const auto& anchor = building_view.get<sim::components::GridPosition>(entity);
            const sim::components::BuildingFootprint footprint =
                resolve_registry_building_footprint(registry, entity);
            const bool is_enemy = registry.any_of<sim::components::PlayerOwnedTag>(entity)
                && sim::components::entity_player_slot(registry, entity) != local_player_slot_;
            float brightness = 1.0F;
            if (shrouded) {
                brightness = constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            }

            const SceneTextureKind building_kind =
                textured_building_kind(is_house, is_lumberjack, is_extractor, is_enemy);
            const int center_x = anchor.cell.x + footprint.width / 2;
            const int center_y = anchor.cell.y + footprint.height / 2;
            const float width_scale = textured_building_width_scale(is_extractor, footprint);
            const float sort_key = static_cast<float>(
                (anchor.cell.x + footprint.width - 1) + (anchor.cell.y + footprint.height - 1))
                + constants::RENDER_BUILDING_SORT_BIAS;
            const float offset_x =
                textured_building_offset_x(is_house, is_lumberjack, is_extractor);
            const float offset_y =
                textured_building_offset_y(is_house, is_lumberjack, is_extractor);
            queue_iso_object_sprite(
                center_x,
                center_y,
                building_kind,
                width_scale,
                brightness,
                sort_key,
                offset_x,
                offset_y);
        }

        const auto unit_view = registry.view<
            sim::components::UnitTag,
            sim::components::GridPosition,
            sim::components::Health>();
        for (const entt::entity entity : unit_view) {
            const auto& health = unit_view.get<sim::components::Health>(entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
            if (fog != nullptr) {
                const core::GridPos visibility_cell{
                    static_cast<int>(std::floor(world_x)),
                    static_cast<int>(std::floor(world_z)),
                };
                if (!sim::systems::is_cell_visible_to_slot(*fog, visibility_cell, local_player_slot_)
                    && (!registry.any_of<sim::components::PlayerOwnedTag>(entity)
                        || sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
                    continue;
                }
            }

            float base_r = 0.25F;
            float base_g = 0.55F;
            float base_b = 0.85F;
            if (registry.any_of<sim::components::EnemyTag>(entity)
                || (registry.any_of<sim::components::PlayerOwnedTag>(entity)
                    && sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
                base_r = 0.85F;
                base_g = 0.25F;
                base_b = 0.25F;
            }
            else if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
                base_r = 0.85F;
                base_g = 0.75F;
                base_b = 0.20F;
            }

            float r = base_r;
            float g = base_g;
            float b = base_b;
            apply_team_color(base_r, base_g, base_b, r, g, b);

            const int unit_tile_x = static_cast<int>(std::floor(world_x));
            const int unit_tile_y = static_cast<int>(std::floor(world_z));
            const float sort_key = cap_unit_sort_below_buildings(
                unit_depth_sort_key(world_x, world_z),
                unit_tile_x,
                unit_tile_y,
                building_anchors,
                building_footprints);

            queue_unit_draw(
                PendingUnitDraw{
                    sort_key,
                    world_x,
                    world_z,
                    r,
                    g,
                    b,
                    r,
                    g,
                    b,
                });
        }

        flush_pending_depth_sorted_draws(active_textured_batch);
        finish_textured_batches();

        glDisable(GL_DEPTH_TEST);
        for (const entt::entity entity : unit_view) {
            const auto& health = unit_view.get<sim::components::Health>(entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
            if (fog != nullptr) {
                const core::GridPos visibility_cell{
                    static_cast<int>(std::floor(world_x)),
                    static_cast<int>(std::floor(world_z)),
                };
                if (!sim::systems::is_cell_visible_to_slot(*fog, visibility_cell, local_player_slot_)
                    && (!registry.any_of<sim::components::PlayerOwnedTag>(entity)
                        || sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
                    continue;
                }
            }

            float base_r = 0.25F;
            float base_g = 0.55F;
            float base_b = 0.85F;
            if (registry.any_of<sim::components::EnemyTag>(entity)
                || (registry.any_of<sim::components::PlayerOwnedTag>(entity)
                    && sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
                base_r = 0.85F;
                base_g = 0.25F;
                base_b = 0.25F;
            }
            else if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
                base_r = 0.85F;
                base_g = 0.75F;
                base_b = 0.20F;
            }

            float silhouette_r = base_r;
            float silhouette_g = base_g;
            float silhouette_b = base_b;
            apply_team_color(base_r, base_g, base_b, silhouette_r, silhouette_g, silhouette_b);

            const std::vector<core::GridPos> occluder_tiles = collect_unit_front_occluder_tiles(
                world_x,
                world_z,
                map,
                building_footprints,
                building_anchors);
            if (!occluder_tiles.empty()) {
                draw_unit_occlusion_silhouette(
                    world_x,
                    world_z,
                    silhouette_r,
                    silhouette_g,
                    silhouette_b,
                    occluder_tiles,
                    building_anchors,
                    building_footprints);
            }
        }
        if (!scene_batch_.empty()) {
            flush_scene_batch();
        }
        glEnable(GL_DEPTH_TEST);
    }

    const auto building_view = registry.view<
        sim::components::BuildingTag,
        sim::components::GridPosition,
        sim::components::Health>();
    for (const entt::entity entity : building_view) {
        const bool visible = fog == nullptr
            || sim::systems::is_entity_visible_to_slot(registry, *fog, entity, local_player_slot_);
        const bool shrouded = is_shrouded_building(entity, visible);
        if (!visible && !shrouded) {
            continue;
        }

        const auto& pos = building_view.get<sim::components::GridPosition>(entity).cell;
        const bool is_enemy = registry.any_of<sim::components::PlayerOwnedTag>(entity)
            && sim::components::entity_player_slot(registry, entity) != local_player_slot_;
        float brightness = 1.0F;
        if (shrouded) {
            brightness = constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
        }

        const bool under_construction = visible
            ? registry.any_of<sim::components::UnderConstructionTag>(entity)
            : remembered_under_construction(entity);
        if (under_construction) {
            continue;
        }

        if (use_textured_tiles
            && (registry.any_of<sim::components::TownCenterTag>(entity)
                || registry.any_of<sim::components::HouseTag>(entity)
                || registry.any_of<sim::components::LumberjackTag>(entity)
                || registry.any_of<sim::components::ExtractorTag>(entity))) {
            continue;
        }

        float base_r = 0.55F;
        float base_g = 0.38F;
        float base_b = 0.18F;
        if (is_enemy) {
            base_r = 0.70F;
            base_g = 0.22F;
            base_b = 0.22F;
        }
        float r = base_r;
        float g = base_g;
        float b = base_b;
        apply_team_color(base_r, base_g, base_b, r, g, b);
        if (shrouded) {
            r *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            g *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            b *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
        }
        draw_entity_prism(
            static_cast<float>(pos.x) + 0.5F,
            static_cast<float>(pos.y) + 0.5F,
            constants::RENDER_BUILDING_HEIGHT,
            r,
            g,
            b);
    }

    finish_textured_batches();

    if (!use_textured_tiles) {
        collect_building_footprints();

        const auto unit_view = registry.view<
            sim::components::UnitTag,
            sim::components::GridPosition,
            sim::components::Health>();
        for (const entt::entity entity : unit_view) {
            const auto& health = unit_view.get<sim::components::Health>(entity);
            if (health.current.raw() <= 0) {
                continue;
            }

            const auto [world_x, world_z] = unit_render_world_xz(registry, entity, interpolation_alpha);
            if (fog != nullptr) {
                const core::GridPos visibility_cell{
                    static_cast<int>(std::floor(world_x)),
                    static_cast<int>(std::floor(world_z)),
                };
                if (!sim::systems::is_cell_visible_to_slot(*fog, visibility_cell, local_player_slot_)
                    && (!registry.any_of<sim::components::PlayerOwnedTag>(entity)
                        || sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
                    continue;
                }
            }

            float base_r = 0.25F;
            float base_g = 0.55F;
            float base_b = 0.85F;
            if (registry.any_of<sim::components::EnemyTag>(entity)
                || (registry.any_of<sim::components::PlayerOwnedTag>(entity)
                    && sim::components::entity_player_slot(registry, entity) != local_player_slot_)) {
                base_r = 0.85F;
                base_g = 0.25F;
                base_b = 0.25F;
            }
            else if (registry.any_of<sim::components::WorkerUnitTag>(entity)) {
                base_r = 0.85F;
                base_g = 0.75F;
                base_b = 0.20F;
            }

            float r = base_r;
            float g = base_g;
            float b = base_b;
            apply_team_color(base_r, base_g, base_b, r, g, b);

            draw_entity_cylinder(
                world_x,
                world_z,
                constants::RENDER_UNIT_HEIGHT,
                constants::RENDER_UNIT_RADIUS,
                r,
                g,
                b);

            const std::vector<core::GridPos> occluder_tiles = collect_unit_front_occluder_tiles(
                world_x,
                world_z,
                map,
                building_footprints,
                building_anchors);
            if (!occluder_tiles.empty()) {
                draw_unit_occlusion_silhouette(
                    world_x,
                    world_z,
                    r,
                    g,
                    b,
                    occluder_tiles,
                    building_anchors,
                    building_footprints);
            }
        }

        if (!scene_batch_.empty()) {
            flush_scene_batch();
        }
    }

    if (use_textured_tiles && show_grid_lines_) {
        finish_textured_batches();
        glDisable(GL_DEPTH_TEST);
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                draw_iso_tile_grid_lines(x, y);
            }
        }
        glEnable(GL_DEPTH_TEST);
    }

    if (!scene_batch_.empty()) {
        flush_scene_batch();
    }

    if (placement_ghost_anchor.has_value()) {
        const int footprint =
            placement_ghost_footprint_tiles(hud_context_input.command_panel_mode);
        if (placement_ghost_footprint_on_map(
                map.width, map.height, *placement_ghost_anchor, footprint)) {
            const float ghost_r = placement_ghost_valid ? 0.35F : 0.85F;
            const float ghost_g = placement_ghost_valid ? 0.75F : 0.25F;
            const float ghost_b = placement_ghost_valid ? 0.45F : 0.25F;
            glDisable(GL_DEPTH_TEST);
            draw_footprint_highlight(
                placement_ghost_anchor->x,
                placement_ghost_anchor->y,
                footprint,
                footprint,
                ghost_r,
                ghost_g,
                ghost_b,
                constants::RENDER_SELECTION_OUTLINE_SCALE);
            const bool placing_house =
                hud_context_input.command_panel_mode == app::CommandPanelMode::PlaceHouse;
            const bool placing_lumberjack =
                hud_context_input.command_panel_mode == app::CommandPanelMode::PlaceLumberjack;
            const bool placing_extractor =
                hud_context_input.command_panel_mode == app::CommandPanelMode::PlaceExtractor;
            const int center_x = placement_ghost_anchor->x + footprint / 2;
            const int center_y = placement_ghost_anchor->y + footprint / 2;
            unsigned int ghost_batch = 0U;
            const SceneTextureKind ghost_kind =
                textured_building_kind(placing_house, placing_lumberjack, placing_extractor, false);
            const float offset_x =
                textured_building_offset_x(placing_house, placing_lumberjack, placing_extractor);
            const float offset_y =
                textured_building_offset_y(placing_house, placing_lumberjack, placing_extractor);
            queue_iso_object_sprite(
                center_x,
                center_y,
                ghost_kind,
                textured_building_width_scale(
                    placing_extractor,
                    sim::components::BuildingFootprint{footprint, footprint}),
                constants::RENDER_GHOST_BUILDING_ALPHA,
                static_cast<float>(
                    placement_ghost_anchor->x + placement_ghost_anchor->y + footprint + footprint
                    - 2)
                    + constants::RENDER_BUILDING_SORT_BIAS,
                offset_x,
                offset_y);
            flush_pending_depth_sorted_draws(ghost_batch);
            glEnable(GL_DEPTH_TEST);
        }
    }

    draw_hitbox_overlays(simulation, interpolation_alpha);
    draw_debug_path_overlays(simulation, interpolation_alpha);

    render::HudUnitContext hud_context = hud_context_input;
    if (selected_entities.size() == 1U) {
        hud_context.selected_single_unit = selected_entities.front();
    }
    else if (selected_building != entt::null) {
        hud_context.selected_single_unit = selected_building;
    }
    else {
        hud_context.selected_single_unit = entt::null;
    }
    hud_context.hover_unit = hover_unit;
    hud_context.hover_unit_is_enemy = hover_unit_is_enemy;
    hud_context.selected_resource_cell = selected_resource_cell;
    {
        const float window_width = static_cast<float>(window_size_.x);
        const float window_height = static_cast<float>(window_size_.y);
        const auto corner0 = camera_.screen_to_world_xz(0.0F, 0.0F);
        const auto corner1 = camera_.screen_to_world_xz(window_width, 0.0F);
        const auto corner2 = camera_.screen_to_world_xz(window_width, window_height);
        const auto corner3 = camera_.screen_to_world_xz(0.0F, window_height);
        hud_context.has_camera_view = true;
        hud_context.camera_world_min_x =
            std::min(std::min(corner0.first, corner1.first), std::min(corner2.first, corner3.first));
        hud_context.camera_world_max_x =
            std::max(std::max(corner0.first, corner1.first), std::max(corner2.first, corner3.first));
        hud_context.camera_world_min_z =
            std::min(std::min(corner0.second, corner1.second), std::min(corner2.second, corner3.second));
        hud_context.camera_world_max_z =
            std::max(std::max(corner0.second, corner1.second), std::max(corner2.second, corner3.second));
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (selection_box.active) {
        draw_screen_rect_outline(
            selection_box.start.x,
            selection_box.start.y,
            selection_box.current.x,
            selection_box.current.y,
            constants::RENDER_SELECTION_BOX_R,
            constants::RENDER_SELECTION_BOX_G,
            constants::RENDER_SELECTION_BOX_B);
    }

    hud_overlay_.draw(
        simulation,
        window_size_,
        fps,
        tps,
        local_player_slot_,
        camera_.zoom(),
        hud_context,
        network_stats,
        show_perf_hud_,
        show_selection_debug_,
        &building_sight_memory_,
        &hud_snapshot);
    if (offscreen) {
        present_target_to_window();
    }
}

void GameRenderer::draw_snapshot(
    const SimRenderSnapshot& snapshot,
    const std::vector<entt::entity>& selected_entities,
    const float interpolation_alpha,
    const SelectionBoxOverlay& selection_box,
    const entt::entity hover_unit,
    const bool hover_unit_is_enemy,
    const entt::entity hover_building,
    const std::optional<core::GridPos> hover_resource_cell,
    const std::optional<core::GridPos> selected_resource_cell,
    const entt::entity selected_building,
    const float fps,
    const float tps,
    const net::LockstepNetworkHudStats& network_stats,
    const HudUnitContext& hud_context_input,
    const std::optional<core::GridPos> placement_ghost_anchor,
    const bool placement_ghost_valid)
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    const bool offscreen = ensure_present_target();
    if (offscreen) {
        glBindFramebuffer(GL_FRAMEBUFFER, present_fbo_);
    }

    glDisable(GL_SCISSOR_TEST);
    if (window_size_.x > 0U && window_size_.y > 0U) {
        glViewport(0, 0, static_cast<GLsizei>(window_size_.x), static_cast<GLsizei>(window_size_.y));
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0U);
    clear_opaque_framebuffer(
        constants::RENDER_CLEAR_R,
        constants::RENDER_CLEAR_G,
        constants::RENDER_CLEAR_B,
        constants::RENDER_CLEAR_A);

    if (snapshot.map_width <= 0 || snapshot.map_height <= 0) {
        if (offscreen) {
            present_target_to_window();
        }
        return;
    }

    if (!map_framed_) {
        try_frame_player_start(snapshot.map_width, snapshot.map_height, nullptr, &snapshot);
    }

    scene_batch_.clear();
    textured_scene_batch_.clear();
    pending_iso_sprites_.clear();
    pending_unit_draws_.clear();

    std::vector<core::GridPos> snapshot_building_anchors{};
    std::vector<sim::components::BuildingFootprint> snapshot_building_footprints{};
    const auto collect_snapshot_building_footprints = [&]() {
        snapshot_building_anchors.clear();
        snapshot_building_footprints.clear();

        for (const RenderEntityPose& pose : snapshot.buildings) {
            if (pose.health_current <= 0) {
                continue;
            }

            snapshot_building_anchors.push_back(core::GridPos{pose.grid_x, pose.grid_y});
            snapshot_building_footprints.push_back(
                sim::components::BuildingFootprint{pose.footprint_width, pose.footprint_height});
        }
    };

    const bool use_textured_tiles = scene_textures_.is_loaded();
    const std::vector<sim::systems::VisionSource> vision_sources =
        ((fog_of_war_enabled_ && !snapshot.fog_visible.empty()) && use_textured_tiles)
        ? collect_vision_sources_from_snapshot(snapshot, local_player_slot_)
        : std::vector<sim::systems::VisionSource>{};
    const std::vector<float>& fog_vertex_brightness =
        ((fog_of_war_enabled_ && !snapshot.fog_visible.empty()) && use_textured_tiles)
        ? ensure_fog_vertex_brightness(
            snapshot.tick_count,
            snapshot.fog_explored,
            snapshot.map_width,
            snapshot.map_height,
            local_player_slot_,
            vision_sources)
        : fog_vertex_cache_;
    const std::vector<float>* fog_vertex_brightness_ptr =
        ((fog_of_war_enabled_ && !snapshot.fog_visible.empty()) && use_textured_tiles
            && !fog_vertex_brightness.empty())
        ? &fog_vertex_brightness
        : nullptr;
    unsigned int active_textured_batch = 0U;

    const auto flush_textured_batch_if_needed = [&](const unsigned int texture_id) {
        if (texture_id == 0U) {
            return;
        }

        if (active_textured_batch != 0U && active_textured_batch != texture_id) {
            flush_textured_scene_batch(active_textured_batch);
        }

        active_textured_batch = texture_id;
    };

    const auto finish_textured_batches = [&]() {
        if (active_textured_batch != 0U) {
            flush_textured_scene_batch(active_textured_batch);
            active_textured_batch = 0U;
        }
    };

    if (use_textured_tiles) {
        draw_batched_ground_tiles(
            snapshot.map_width,
            snapshot.map_height,
            snapshot.ground,
            snapshot.fog_visible,
            snapshot.fog_explored,
            fog_of_war_enabled_ && !snapshot.fog_visible.empty(),
            fog_vertex_brightness_ptr,
            snapshot.tick_count);
    }
    else {
        for (int y = 0; y < snapshot.map_height; ++y) {
            for (int x = 0; x < snapshot.map_width; ++x) {
                const int index = y * snapshot.map_width + x;
                const auto live_tile = snapshot.tiles[static_cast<std::size_t>(index)];
                const int live_forest_wood = snapshot.forest_wood[static_cast<std::size_t>(index)];
                const bool use_memory = (fog_of_war_enabled_ && !snapshot.fog_visible.empty())
                    && !snapshot.fog_memory_tiles.empty()
                    && fog_tile_is_in_shroud(
                        snapshot.fog_visible,
                        snapshot.fog_explored,
                        snapshot.map_width,
                        snapshot.map_height,
                        local_player_slot_,
                        x,
                        y);
                const sim::components::TileType memory_tile =
                    use_memory
                    ? static_cast<sim::components::TileType>(
                        snapshot.fog_memory_tiles[static_cast<std::size_t>(index)])
                    : live_tile;
                const int memory_forest_wood =
                    use_memory && static_cast<std::size_t>(index) < snapshot.fog_memory_forest_wood.size()
                    ? snapshot.fog_memory_forest_wood[static_cast<std::size_t>(index)]
                    : live_forest_wood;

                sim::components::TileType tile = live_tile;
                int forest_wood = live_forest_wood;
                float r = 0.20F;
                float g = 0.32F;
                float b = 0.18F;
                float extrude = 0.0F;
                resolve_tile_appearance(
                    live_tile,
                    live_forest_wood,
                    use_memory,
                    memory_tile,
                    memory_forest_wood,
                    tile,
                    forest_wood,
                    r,
                    g,
                    b,
                    extrude);

                float brightness = 1.0F;
                if ((fog_of_war_enabled_ && !snapshot.fog_visible.empty())) {
                    brightness = fog_tile_brightness(
                        snapshot.fog_visible,
                        snapshot.fog_explored,
                        snapshot.map_width,
                        snapshot.map_height,
                        local_player_slot_,
                        x,
                        y);
                }

                r *= brightness;
                g *= brightness;
                b *= brightness;

                draw_extruded_tile(x, y, extrude, r, g, b);
                if (show_grid_lines_ && (r > 0.0F || g > 0.0F || b > 0.0F)) {
                    draw_tile_grid_lines(x, y, extrude);
                }
            }
        }
    }

    finish_textured_batches();

    if (!scene_batch_.empty()) {
        flush_scene_batch();
    }

    draw_interaction_highlights(
        nullptr,
        &snapshot,
        selected_entities,
        hover_unit,
        hover_unit_is_enemy,
        hover_building,
        hover_resource_cell,
        selected_resource_cell,
        selected_building,
        interpolation_alpha);

    if (use_textured_tiles) {
        collect_snapshot_building_footprints();

        for (int y = 0; y < snapshot.map_height; ++y) {
            for (int x = 0; x < snapshot.map_width; ++x) {
                if (!iso_tile_intersects_window(
                        x,
                        y,
                        constants::RENDER_OBJECT_SCREEN_CULL_PAD_TILES)) {
                    continue;
                }

                if ((fog_of_war_enabled_ && !snapshot.fog_visible.empty())
                    && is_fog_tile_unexplored(
                        snapshot.fog_visible,
                        snapshot.fog_explored,
                        snapshot.map_width,
                        snapshot.map_height,
                        local_player_slot_,
                        x,
                        y)) {
                    continue;
                }

                if (cell_covered_by_building_footprint(
                        x,
                        y,
                        snapshot_building_anchors,
                        snapshot_building_footprints)) {
                    continue;
                }

                const int index = y * snapshot.map_width + x;
                const auto live_tile = snapshot.tiles[static_cast<std::size_t>(index)];
                const int live_forest_wood = snapshot.forest_wood[static_cast<std::size_t>(index)];
                const bool use_memory = (fog_of_war_enabled_ && !snapshot.fog_visible.empty())
                    && !snapshot.fog_memory_tiles.empty()
                    && fog_tile_is_in_shroud(
                        snapshot.fog_visible,
                        snapshot.fog_explored,
                        snapshot.map_width,
                        snapshot.map_height,
                        local_player_slot_,
                        x,
                        y);
                const sim::components::TileType memory_tile =
                    use_memory
                    ? static_cast<sim::components::TileType>(
                        snapshot.fog_memory_tiles[static_cast<std::size_t>(index)])
                    : live_tile;
                const int memory_forest_wood =
                    use_memory
                    && static_cast<std::size_t>(index) < snapshot.fog_memory_forest_wood.size()
                    ? snapshot.fog_memory_forest_wood[static_cast<std::size_t>(index)]
                    : live_forest_wood;

                sim::components::TileType tile = live_tile;
                int forest_wood = live_forest_wood;
                float unused_r = 0.0F;
                float unused_g = 0.0F;
                float unused_b = 0.0F;
                float unused_extrude = 0.0F;
                resolve_tile_appearance(
                    live_tile,
                    live_forest_wood,
                    use_memory,
                    memory_tile,
                    memory_forest_wood,
                    tile,
                    forest_wood,
                    unused_r,
                    unused_g,
                    unused_b,
                    unused_extrude);

                const int live_bush_food =
                    static_cast<std::size_t>(index) < snapshot.bush_food.size()
                    ? snapshot.bush_food[static_cast<std::size_t>(index)]
                    : 0;
                const int memory_bush_food =
                    use_memory
                        && static_cast<std::size_t>(index) < snapshot.fog_memory_bush_food.size()
                    ? snapshot.fog_memory_bush_food[static_cast<std::size_t>(index)]
                    : live_bush_food;
                const int bush_food = use_memory ? memory_bush_food : live_bush_food;
                const int live_mine_money =
                    static_cast<std::size_t>(index) < snapshot.mine_money.size()
                    ? snapshot.mine_money[static_cast<std::size_t>(index)]
                    : 0;
                const int memory_mine_money =
                    use_memory
                        && static_cast<std::size_t>(index) < snapshot.fog_memory_mine_money.size()
                    ? snapshot.fog_memory_mine_money[static_cast<std::size_t>(index)]
                    : live_mine_money;
                const int mine_money = use_memory ? memory_mine_money : live_mine_money;
                const bool draw_forest = tile == sim::components::TileType::Forest && forest_wood > 0;
                const bool draw_bush =
                    (tile == sim::components::TileType::Berries
                        || tile == sim::components::TileType::Blueberries)
                    && bush_food > 0;
                const bool draw_gold_mine =
                    tile == sim::components::TileType::GoldMine && mine_money > 0;
                if (!draw_forest && !draw_bush && !draw_gold_mine) {
                    continue;
                }

                float brightness = 1.0F;
                if ((fog_of_war_enabled_ && !snapshot.fog_visible.empty())) {
                    brightness = fog_tile_brightness(
                        snapshot.fog_visible,
                        snapshot.fog_explored,
                        snapshot.map_width,
                        snapshot.map_height,
                        local_player_slot_,
                        x,
                        y);
                }
                brightness = fog_object_brightness(
                    fog_vertex_brightness_ptr,
                    snapshot.map_width,
                    snapshot.map_height,
                    x,
                    y,
                    brightness);

                const SceneTextureKind object_kind = draw_forest
                    ? forest_tree_texture_for_cell(
                        x,
                        y,
                        static_cast<std::size_t>(index) < snapshot.ground.size()
                            ? snapshot.ground[static_cast<std::size_t>(index)]
                            : sim::components::GroundType::Grass)
                    : (draw_gold_mine
                        ? gold_mine_texture_for_cell(x, y)
                        : (tile == sim::components::TileType::Blueberries
                            ? SceneTextureKind::Blueberries
                            : SceneTextureKind::Berries));
                const float width_scale = draw_gold_mine
                    ? constants::RENDER_GOLD_MINE_SPRITE_WIDTH_SCALE
                    : (draw_forest
                        ? tree_sprite_width_scale(object_kind)
                        : constants::RENDER_TREE_SPRITE_WIDTH_SCALE);
                float offset_x = 0.0F;
                float offset_y = 0.0F;
                if (draw_gold_mine) {
                    offset_x = constants::RENDER_GOLD_MINE_SPRITE_OFFSET_X;
                    offset_y = constants::RENDER_GOLD_MINE_SPRITE_OFFSET_Y;
                }
                else if (draw_forest) {
                    const auto tree_offset = tree_sprite_offset_for_kind(object_kind);
                    offset_x = tree_offset.first;
                    offset_y = tree_offset.second;
                }
                else {
                    offset_x = constants::RENDER_BERRY_SPRITE_OFFSET_X;
                    offset_y = constants::RENDER_BERRY_SPRITE_OFFSET_Y;
                }
                queue_iso_object_sprite(
                    x,
                    y,
                    object_kind,
                    width_scale,
                    brightness,
                    static_cast<float>(x + y) + constants::RENDER_TREE_SORT_BIAS,
                    offset_x,
                    offset_y);
            }
        }

        const int snapshot_mana_lake_frame = mana_lake_animation_frame();
        for (const RenderEntityPose& pose : snapshot.buildings) {
            if (!pose.is_mana_lake) {
                continue;
            }

            bool covered_by_completed_extractor = false;
            for (const RenderEntityPose& other : snapshot.buildings) {
                if (!other.is_extractor || other.under_construction || other.health_current <= 0) {
                    continue;
                }

                if (other.grid_x == pose.grid_x && other.grid_y == pose.grid_y) {
                    covered_by_completed_extractor = true;
                    break;
                }
            }
            if (covered_by_completed_extractor) {
                continue;
            }

            queue_iso_object_sprite(
                pose.grid_x + pose.footprint_width / 2,
                pose.grid_y + pose.footprint_height / 2,
                SceneTextureKind::ManaLake,
                constants::RENDER_MANA_LAKE_SPRITE_WIDTH_SCALE,
                snapshot_cell_is_visible(snapshot, core::GridPos{pose.grid_x, pose.grid_y})
                    ? 1.0F
                    : constants::FOG_EXPLORED_SHROUD_BRIGHTNESS,
                static_cast<float>(
                    (pose.grid_x + pose.footprint_width - 1)
                    + (pose.grid_y + pose.footprint_height - 1))
                    + constants::RENDER_MANA_LAKE_SORT_BIAS,
                constants::RENDER_MANA_LAKE_SPRITE_OFFSET_X,
                constants::RENDER_MANA_LAKE_SPRITE_OFFSET_Y,
                snapshot_mana_lake_frame,
                constants::MANA_LAKE_ANIMATION_FRAME_COUNT);
        }

        for (const RenderEntityPose& pose : snapshot.buildings) {
            if ((!pose.is_town_center && !pose.is_house && !pose.is_lumberjack
                    && !pose.is_extractor)
                || pose.health_current <= 0
                || pose.under_construction) {
                continue;
            }

            float brightness = 1.0F;
            if (pose.shrouded) {
                brightness = constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            }

            const bool is_enemy = pose.player_slot != local_player_slot_;
            const SceneTextureKind building_kind =
                textured_building_kind(pose.is_house, pose.is_lumberjack, pose.is_extractor, is_enemy);
            const int center_x = pose.grid_x + pose.footprint_width / 2;
            const int center_y = pose.grid_y + pose.footprint_height / 2;
            const sim::components::BuildingFootprint footprint{
                pose.footprint_width,
                pose.footprint_height,
            };
            const float width_scale =
                textured_building_width_scale(pose.is_extractor, footprint);
            const float sort_key = static_cast<float>(
                (pose.grid_x + pose.footprint_width - 1) + (pose.grid_y + pose.footprint_height - 1))
                + constants::RENDER_BUILDING_SORT_BIAS;
            const float offset_x =
                textured_building_offset_x(pose.is_house, pose.is_lumberjack, pose.is_extractor);
            const float offset_y =
                textured_building_offset_y(pose.is_house, pose.is_lumberjack, pose.is_extractor);
            queue_iso_object_sprite(
                center_x,
                center_y,
                building_kind,
                width_scale,
                brightness,
                sort_key,
                offset_x,
                offset_y);
        }

        sim::components::MapGrid snapshot_occlusion_map{};
        snapshot_occlusion_map.width = snapshot.map_width;
        snapshot_occlusion_map.height = snapshot.map_height;
        snapshot_occlusion_map.tiles = snapshot.tiles;
        snapshot_occlusion_map.forest_wood = snapshot.forest_wood;
        snapshot_occlusion_map.bush_food = snapshot.bush_food;
        snapshot_occlusion_map.mine_money = snapshot.mine_money;

        for (const RenderEntityPose& pose : snapshot.units) {
            if (pose.health_current <= 0) {
                continue;
            }

            const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
            if ((fog_of_war_enabled_ && !snapshot.fog_visible.empty())) {
                const core::GridPos visibility_cell =
                    snapshot_world_visibility_cell(world_x, world_z);
                if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                    continue;
                }
            }

            float base_r = 0.25F;
            float base_g = 0.55F;
            float base_b = 0.85F;
            if (pose.is_enemy) {
                base_r = 0.85F;
                base_g = 0.25F;
                base_b = 0.25F;
            }
            else if (pose.is_worker) {
                base_r = 0.85F;
                base_g = 0.75F;
                base_b = 0.20F;
            }

            float r = base_r;
            float g = base_g;
            float b = base_b;
            apply_team_color(base_r, base_g, base_b, r, g, b);
            const int unit_tile_x = static_cast<int>(std::floor(world_x));
            const int unit_tile_y = static_cast<int>(std::floor(world_z));
            const float sort_key = cap_unit_sort_below_buildings(
                unit_depth_sort_key(world_x, world_z),
                unit_tile_x,
                unit_tile_y,
                snapshot_building_anchors,
                snapshot_building_footprints);
            queue_unit_draw(
                PendingUnitDraw{
                    sort_key,
                    world_x,
                    world_z,
                    r,
                    g,
                    b,
                    r,
                    g,
                    b,
                });
        }

        flush_pending_depth_sorted_draws(active_textured_batch);
        finish_textured_batches();

        glDisable(GL_DEPTH_TEST);
        for (const RenderEntityPose& pose : snapshot.units) {
            if (pose.health_current <= 0) {
                continue;
            }

            const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
            if ((fog_of_war_enabled_ && !snapshot.fog_visible.empty())) {
                const core::GridPos visibility_cell =
                    snapshot_world_visibility_cell(world_x, world_z);
                if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                    continue;
                }
            }

            float base_r = 0.25F;
            float base_g = 0.55F;
            float base_b = 0.85F;
            if (pose.is_enemy) {
                base_r = 0.85F;
                base_g = 0.25F;
                base_b = 0.25F;
            }
            else if (pose.is_worker) {
                base_r = 0.85F;
                base_g = 0.75F;
                base_b = 0.20F;
            }

            float silhouette_r = base_r;
            float silhouette_g = base_g;
            float silhouette_b = base_b;
            apply_team_color(base_r, base_g, base_b, silhouette_r, silhouette_g, silhouette_b);

            const std::vector<core::GridPos> occluder_tiles = collect_unit_front_occluder_tiles(
                world_x,
                world_z,
                snapshot_occlusion_map,
                snapshot_building_footprints,
                snapshot_building_anchors);
            if (!occluder_tiles.empty()) {
                draw_unit_occlusion_silhouette(
                    world_x,
                    world_z,
                    silhouette_r,
                    silhouette_g,
                    silhouette_b,
                    occluder_tiles,
                    snapshot_building_anchors,
                    snapshot_building_footprints);
            }
        }
        if (!scene_batch_.empty()) {
            flush_scene_batch();
        }
        glEnable(GL_DEPTH_TEST);
    }

    for (const RenderEntityPose& pose : snapshot.buildings) {
        if (pose.is_mana_lake) {
            continue;
        }

        if (use_textured_tiles
            && (pose.is_town_center || pose.is_house || pose.is_lumberjack || pose.is_extractor)) {
            continue;
        }

        float base_r = 0.55F;
        float base_g = 0.38F;
        float base_b = 0.18F;
        if (pose.player_slot != local_player_slot_) {
            base_r = 0.70F;
            base_g = 0.22F;
            base_b = 0.22F;
        }

        float r = base_r;
        float g = base_g;
        float b = base_b;
        apply_team_color(base_r, base_g, base_b, r, g, b);
        if (pose.shrouded) {
            r *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            g *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
            b *= constants::FOG_EXPLORED_SHROUD_BRIGHTNESS;
        }
        draw_entity_prism(
            static_cast<float>(pose.grid_x) + 0.5F,
            static_cast<float>(pose.grid_y) + 0.5F,
            constants::RENDER_BUILDING_HEIGHT,
            r,
            g,
            b);
    }

    finish_textured_batches();

    if (!use_textured_tiles) {
        collect_snapshot_building_footprints();

        sim::components::MapGrid snapshot_occlusion_map{};
        snapshot_occlusion_map.width = snapshot.map_width;
        snapshot_occlusion_map.height = snapshot.map_height;
        snapshot_occlusion_map.tiles = snapshot.tiles;
        snapshot_occlusion_map.forest_wood = snapshot.forest_wood;
        snapshot_occlusion_map.bush_food = snapshot.bush_food;
        snapshot_occlusion_map.mine_money = snapshot.mine_money;

        for (const RenderEntityPose& pose : snapshot.units) {
            if (pose.health_current <= 0) {
                continue;
            }

            const auto [world_x, world_z] = interpolate_render_pose(pose, interpolation_alpha);
            if ((fog_of_war_enabled_ && !snapshot.fog_visible.empty())) {
                const core::GridPos visibility_cell =
                    snapshot_world_visibility_cell(world_x, world_z);
                if (!snapshot_cell_is_visible(snapshot, visibility_cell)) {
                    continue;
                }
            }

            float base_r = 0.25F;
            float base_g = 0.55F;
            float base_b = 0.85F;
            if (pose.is_enemy) {
                base_r = 0.85F;
                base_g = 0.25F;
                base_b = 0.25F;
            }
            else if (pose.is_worker) {
                base_r = 0.85F;
                base_g = 0.75F;
                base_b = 0.20F;
            }

            float r = base_r;
            float g = base_g;
            float b = base_b;
            apply_team_color(base_r, base_g, base_b, r, g, b);
            draw_entity_cylinder(
                world_x,
                world_z,
                constants::RENDER_UNIT_HEIGHT,
                constants::RENDER_UNIT_RADIUS,
                r,
                g,
                b);

            const std::vector<core::GridPos> occluder_tiles = collect_unit_front_occluder_tiles(
                world_x,
                world_z,
                snapshot_occlusion_map,
                snapshot_building_footprints,
                snapshot_building_anchors);
            if (!occluder_tiles.empty()) {
                draw_unit_occlusion_silhouette(
                    world_x,
                    world_z,
                    r,
                    g,
                    b,
                    occluder_tiles,
                    snapshot_building_anchors,
                    snapshot_building_footprints);
            }
        }

        if (!scene_batch_.empty()) {
            flush_scene_batch();
        }
    }

    if (use_textured_tiles && show_grid_lines_) {
        finish_textured_batches();
        glDisable(GL_DEPTH_TEST);
        for (int y = 0; y < snapshot.map_height; ++y) {
            for (int x = 0; x < snapshot.map_width; ++x) {
                draw_iso_tile_grid_lines(x, y);
            }
        }
        glEnable(GL_DEPTH_TEST);
    }

    if (!scene_batch_.empty()) {
        flush_scene_batch();
    }

    if (placement_ghost_anchor.has_value()) {
        const int footprint =
            placement_ghost_footprint_tiles(hud_context_input.command_panel_mode);
        if (placement_ghost_footprint_on_map(
                snapshot.map_width,
                snapshot.map_height,
                *placement_ghost_anchor,
                footprint)) {
            const float ghost_r = placement_ghost_valid ? 0.35F : 0.85F;
            const float ghost_g = placement_ghost_valid ? 0.75F : 0.25F;
            const float ghost_b = placement_ghost_valid ? 0.45F : 0.25F;
            glDisable(GL_DEPTH_TEST);
            draw_footprint_highlight(
                placement_ghost_anchor->x,
                placement_ghost_anchor->y,
                footprint,
                footprint,
                ghost_r,
                ghost_g,
                ghost_b,
                constants::RENDER_SELECTION_OUTLINE_SCALE);
            const bool placing_house =
                hud_context_input.command_panel_mode == app::CommandPanelMode::PlaceHouse;
            const bool placing_lumberjack =
                hud_context_input.command_panel_mode == app::CommandPanelMode::PlaceLumberjack;
            const bool placing_extractor =
                hud_context_input.command_panel_mode == app::CommandPanelMode::PlaceExtractor;
            const int center_x = placement_ghost_anchor->x + footprint / 2;
            const int center_y = placement_ghost_anchor->y + footprint / 2;
            unsigned int ghost_batch = 0U;
            const SceneTextureKind ghost_kind =
                textured_building_kind(placing_house, placing_lumberjack, placing_extractor, false);
            const float offset_x =
                textured_building_offset_x(placing_house, placing_lumberjack, placing_extractor);
            const float offset_y =
                textured_building_offset_y(placing_house, placing_lumberjack, placing_extractor);
            queue_iso_object_sprite(
                center_x,
                center_y,
                ghost_kind,
                textured_building_width_scale(
                    placing_extractor,
                    sim::components::BuildingFootprint{footprint, footprint}),
                constants::RENDER_GHOST_BUILDING_ALPHA,
                static_cast<float>(
                    placement_ghost_anchor->x + placement_ghost_anchor->y + footprint + footprint
                    - 2)
                    + constants::RENDER_BUILDING_SORT_BIAS,
                offset_x,
                offset_y);
            flush_pending_depth_sorted_draws(ghost_batch);
            glEnable(GL_DEPTH_TEST);
        }
    }

    draw_hitbox_overlays_snapshot(snapshot, interpolation_alpha);
    draw_debug_path_overlays_snapshot(snapshot, interpolation_alpha);

    render::HudUnitContext hud_context = hud_context_input;
    if (selected_entities.size() == 1U) {
        hud_context.selected_single_unit = selected_entities.front();
    }
    else if (selected_building != entt::null) {
        hud_context.selected_single_unit = selected_building;
    }
    else {
        hud_context.selected_single_unit = entt::null;
    }
    hud_context.hover_unit = hover_unit;
    hud_context.hover_unit_is_enemy = hover_unit_is_enemy;
    hud_context.selected_resource_cell = selected_resource_cell;
    {
        const float window_width = static_cast<float>(window_size_.x);
        const float window_height = static_cast<float>(window_size_.y);
        const auto corner0 = camera_.screen_to_world_xz(0.0F, 0.0F);
        const auto corner1 = camera_.screen_to_world_xz(window_width, 0.0F);
        const auto corner2 = camera_.screen_to_world_xz(window_width, window_height);
        const auto corner3 = camera_.screen_to_world_xz(0.0F, window_height);
        hud_context.has_camera_view = true;
        hud_context.camera_world_min_x =
            std::min(std::min(corner0.first, corner1.first), std::min(corner2.first, corner3.first));
        hud_context.camera_world_max_x =
            std::max(std::max(corner0.first, corner1.first), std::max(corner2.first, corner3.first));
        hud_context.camera_world_min_z =
            std::min(std::min(corner0.second, corner1.second), std::min(corner2.second, corner3.second));
        hud_context.camera_world_max_z =
            std::max(std::max(corner0.second, corner1.second), std::max(corner2.second, corner3.second));
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (selection_box.active) {
        draw_screen_rect_outline(
            selection_box.start.x,
            selection_box.start.y,
            selection_box.current.x,
            selection_box.current.y,
            constants::RENDER_SELECTION_BOX_R,
            constants::RENDER_SELECTION_BOX_G,
            constants::RENDER_SELECTION_BOX_B);
    }

    hud_overlay_.draw_snapshot(
        snapshot,
        window_size_,
        fps,
        tps,
        local_player_slot_,
        camera_.zoom(),
        hud_context,
        network_stats,
        show_perf_hud_,
        show_selection_debug_);
    if (offscreen) {
        present_target_to_window();
    }
}

void GameRenderer::clear_frame() const
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    glDisable(GL_SCISSOR_TEST);
    clear_opaque_framebuffer(
        constants::RENDER_CLEAR_R,
        constants::RENDER_CLEAR_G,
        constants::RENDER_CLEAR_B,
        constants::RENDER_CLEAR_A);
}

void GameRenderer::draw_waiting_overlay(
    const std::string& title,
    const std::string& subtitle) const
{
    if (active_camera_view() != CameraView::Classic) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    hud_overlay_.draw_waiting_overlay(window_size_, title, subtitle);
    glEnable(GL_DEPTH_TEST);
}

} // namespace aoa::render
