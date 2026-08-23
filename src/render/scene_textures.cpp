#include "render/scene_textures.hpp"

#include "core/asset_io.hpp"
#include "core/asset_store.hpp"

#include <glad/glad.h>

#include <SFML/Graphics/Image.hpp>

#include <iostream>
#include <nlohmann/json.hpp>
#include <utility>

namespace aoa::render {

namespace {

[[nodiscard]] std::string json_string_at(
    const nlohmann::json& root,
    const char* section,
    const char* key)
{
    if (!root.contains(section) || !root[section].contains(key)) {
        return {};
    }

    return root[section][key].get<std::string>();
}

} // namespace

SceneTextureCatalog::SceneTextureCatalog() = default;

SceneTextureCatalog::~SceneTextureCatalog()
{
    destroy_gl_resources();
}

void SceneTextureCatalog::destroy_gl_resources()
{
    for (TextureEntry& entry : textures_) {
        if (entry.gl_texture_id != 0U) {
            glDeleteTextures(1, &entry.gl_texture_id);
            entry.gl_texture_id = 0U;
        }

        entry.valid = false;
        entry.width = 0;
        entry.height = 0;
    }

    loaded_ = false;
}

bool SceneTextureCatalog::load_texture_file(
    const std::filesystem::path& /*assets_directory*/,
    const std::string& relative_path,
    TextureEntry& entry,
    const int crop_x,
    const int crop_y,
    const int crop_width,
    const int crop_height)
{
    if (relative_path.empty()) {
        return false;
    }

    sf::Image image{};
    if (!core::load_image_asset(image, relative_path)) {
        std::cerr << "scene textures: failed to load " << relative_path << '\n';
        return false;
    }

    if (crop_width > 0 && crop_height > 0) {
        const sf::Vector2u source_size = image.getSize();
        const unsigned int crop_left = static_cast<unsigned int>(crop_x);
        const unsigned int crop_top = static_cast<unsigned int>(crop_y);
        const unsigned int crop_w = static_cast<unsigned int>(crop_width);
        const unsigned int crop_h = static_cast<unsigned int>(crop_height);
        if (crop_left + crop_w > source_size.x || crop_top + crop_h > source_size.y) {
            std::cerr << "scene textures: crop out of range for " << relative_path << '\n';
            return false;
        }

        sf::Image cropped(sf::Vector2u{crop_w, crop_h}, sf::Color::Transparent);
        if (!cropped.copy(
                image,
                {0U, 0U},
                sf::IntRect(
                    {crop_x, crop_y},
                    {crop_width, crop_height}))) {
            std::cerr << "scene textures: crop copy failed for " << relative_path << '\n';
            return false;
        }
        image = std::move(cropped);
    }

    const sf::Vector2u size = image.getSize();
    if (size.x == 0U || size.y == 0U) {
        return false;
    }

    image.flipVertically();

    unsigned int texture_id = 0U;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<int>(size.x),
        static_cast<int>(size.y),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.getPixelsPtr());
    glBindTexture(GL_TEXTURE_2D, 0U);

    entry.gl_texture_id = texture_id;
    entry.width = static_cast<int>(size.x);
    entry.height = static_cast<int>(size.y);
    entry.valid = true;
    return true;
}

void SceneTextureCatalog::load(const std::filesystem::path& assets_directory)
{
    destroy_gl_resources();
    (void)assets_directory;

    const auto manifest_text = core::AssetStore::instance().read_text("visuals.json");
    if (!manifest_text.has_value()) {
        std::cerr << "scene textures: missing or invalid visuals.json\n";
        return;
    }

    nlohmann::json manifest{};
    try {
        manifest = nlohmann::json::parse(*manifest_text);
    }
    catch (const std::exception& exception) {
        std::cerr << "scene textures: invalid visuals.json: " << exception.what() << '\n';
        return;
    }

    struct Mapping {
        SceneTextureKind kind;
        const char* section;
        const char* key;
        int crop_x;
        int crop_y;
        int crop_width;
        int crop_height;
    };

    const Mapping mappings[] = {
        {SceneTextureKind::Grass, "tiles", "grass", 0, 0, 0, 0},
        {SceneTextureKind::GrassVariant, "tiles", "grass_variant", 0, 0, 0, 0},
        {SceneTextureKind::Dirt, "tiles", "dirt", 0, 0, 0, 0},
        {SceneTextureKind::DirtVariant, "tiles", "dirt_variant", 0, 0, 0, 0},
        {SceneTextureKind::Snow, "tiles", "snow", 0, 0, 0, 0},
        {SceneTextureKind::Sand, "tiles", "sand", 0, 0, 0, 0},
        {SceneTextureKind::OakForestSmall, "tiles", "oak_forest_small", 0, 0, 0, 0},
        {SceneTextureKind::OakForestMedium, "tiles", "oak_forest_medium", 0, 0, 0, 0},
        {SceneTextureKind::OakForestLarge, "tiles", "oak_forest_large", 0, 0, 0, 0},
        {SceneTextureKind::DarkenedOakForestSmall, "tiles", "darkened_oak_forest_small", 0, 0, 0, 0},
        {SceneTextureKind::DarkenedOakForestMedium, "tiles", "darkened_oak_forest_medium", 0, 0, 0, 0},
        {SceneTextureKind::DarkenedOakForestLarge, "tiles", "darkened_oak_forest_large", 0, 0, 0, 0},
        {SceneTextureKind::PinesForestSmall, "tiles", "pines_forest_small", 0, 0, 0, 0},
        {SceneTextureKind::PinesForestMedium, "tiles", "pines_forest_medium", 0, 0, 0, 0},
        {SceneTextureKind::PinesForestLarge, "tiles", "pines_forest_large", 0, 0, 0, 0},
        {SceneTextureKind::DarkenedPinesForestSmall, "tiles", "darkened_pines_forest_small", 0, 0, 0, 0},
        {SceneTextureKind::DarkenedPinesForestMedium, "tiles", "darkened_pines_forest_medium", 0, 0, 0, 0},
        {SceneTextureKind::DarkenedPinesForestLarge, "tiles", "darkened_pines_forest_large", 0, 0, 0, 0},
        {SceneTextureKind::ForestStump, "tiles", "forest_stump", 0, 0, 0, 0},
        {SceneTextureKind::Berries, "tiles", "berries", 0, 0, 0, 0},
        {SceneTextureKind::Blueberries, "tiles", "blueberries", 0, 0, 0, 0},
        {SceneTextureKind::GoldMine0, "tiles", "gold_mine_0", 0, 0, 0, 0},
        {SceneTextureKind::GoldMine1, "tiles", "gold_mine_1", 0, 0, 0, 0},
        {SceneTextureKind::GoldMine2, "tiles", "gold_mine_2", 0, 0, 0, 0},
        {SceneTextureKind::GoldMine3, "tiles", "gold_mine_3", 0, 0, 0, 0},
        {SceneTextureKind::Rock1, "tiles", "rock_1", 0, 0, 0, 0},
        {SceneTextureKind::Rock2, "tiles", "rock_2", 0, 0, 0, 0},
        {SceneTextureKind::Rock3, "tiles", "rock_3", 0, 0, 0, 0},
        {SceneTextureKind::TownCenter, "buildings", "town_center", 0, 0, 0, 0},
        {SceneTextureKind::TownCenterMask, "buildings", "town_center_mask", 0, 0, 0, 0},
        {SceneTextureKind::HouseA, "buildings", "house_a", 0, 0, 0, 0},
        {SceneTextureKind::HouseAMask, "buildings", "house_a_mask", 0, 0, 0, 0},
        {SceneTextureKind::HouseB, "buildings", "house_b", 0, 0, 0, 0},
        {SceneTextureKind::HouseBMask, "buildings", "house_b_mask", 0, 0, 0, 0},
        {SceneTextureKind::HouseC, "buildings", "house_c", 0, 0, 0, 0},
        {SceneTextureKind::HouseCMask, "buildings", "house_c_mask", 0, 0, 0, 0},
        {SceneTextureKind::LumberCamp, "buildings", "lumber_camp", 0, 0, 0, 0},
        {SceneTextureKind::LumberCampMask, "buildings", "lumber_camp_mask", 0, 0, 0, 0},
        {SceneTextureKind::Mill, "buildings", "mill", 0, 0, 0, 0},
        {SceneTextureKind::MillMask, "buildings", "mill_mask", 0, 0, 0, 0},
        {SceneTextureKind::MiningCamp, "buildings", "mining_camp", 0, 0, 0, 0},
        {SceneTextureKind::MiningCampMask, "buildings", "mining_camp_mask", 0, 0, 0, 0},
        {SceneTextureKind::Barracks, "buildings", "barracks", 0, 0, 0, 0},
        {SceneTextureKind::BarracksMask, "buildings", "barracks_mask", 0, 0, 0, 0},
        {SceneTextureKind::MageAcademy, "buildings", "mage_academy", 0, 0, 0, 0},
        {SceneTextureKind::MageAcademyMask, "buildings", "mage_academy_mask", 0, 0, 0, 0},
        {SceneTextureKind::Tower, "buildings", "tower", 0, 0, 0, 0},
        {SceneTextureKind::TowerMask, "buildings", "tower_mask", 0, 0, 0, 0},
        {SceneTextureKind::Market, "buildings", "market", 0, 0, 0, 0},
        {SceneTextureKind::MarketMask, "buildings", "market_mask", 0, 0, 0, 0},
        {SceneTextureKind::Extractor, "buildings", "extractor", 0, 0, 0, 0},
        {SceneTextureKind::ExtractorMask, "buildings", "extractor_mask", 0, 0, 0, 0},
        {SceneTextureKind::Garden, "buildings", "garden", 0, 0, 0, 0},
        {SceneTextureKind::GardenMask, "buildings", "garden_mask", 0, 0, 0, 0},
        {SceneTextureKind::Reservoir, "buildings", "reservoir", 0, 0, 0, 0},
        {SceneTextureKind::ReservoirMask, "buildings", "reservoir_mask", 0, 0, 0, 0},
        {SceneTextureKind::Farm, "buildings", "farm", 0, 0, 0, 0},
        {SceneTextureKind::FarmDepleted, "buildings", "farm_depleted", 0, 0, 0, 0},
        {SceneTextureKind::FarmDepletedMask, "buildings", "farm_depleted_mask", 0, 0, 0, 0},
        {SceneTextureKind::ManaLake, "nature", "mana_lake", 0, 0, 0, 0},
    };

    int loaded_count = 0;
    for (const Mapping& mapping : mappings) {
        const std::string relative_path = json_string_at(manifest, mapping.section, mapping.key);
        if (load_texture_file(
                assets_directory,
                relative_path,
                textures_[static_cast<std::size_t>(mapping.kind)],
                mapping.crop_x,
                mapping.crop_y,
                mapping.crop_width,
                mapping.crop_height)) {
            ++loaded_count;
        }
    }

    loaded_ = loaded_count > 0;
    if (loaded_) {
        std::cout << "scene textures: loaded " << loaded_count << " textures\n";
    }
}

bool SceneTextureCatalog::is_loaded() const
{
    return loaded_;
}

unsigned int SceneTextureCatalog::texture_id(const SceneTextureKind kind) const
{
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= textures_.size() || !textures_[index].valid) {
        return 0U;
    }

    return textures_[index].gl_texture_id;
}

float SceneTextureCatalog::aspect_ratio(const SceneTextureKind kind) const
{
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= textures_.size() || !textures_[index].valid || textures_[index].width <= 0) {
        return 1.0F;
    }

    return static_cast<float>(textures_[index].height)
        / static_cast<float>(textures_[index].width);
}

} // namespace aoa::render
