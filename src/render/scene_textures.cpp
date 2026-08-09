#include "render/scene_textures.hpp"

#include "core/asset_io.hpp"
#include "core/asset_store.hpp"

#include <glad/glad.h>

#include <SFML/Graphics/Image.hpp>

#include <iostream>
#include <nlohmann/json.hpp>

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
    TextureEntry& entry)
{
    if (relative_path.empty()) {
        return false;
    }

    sf::Image image{};
    if (!core::load_image_asset(image, relative_path)) {
        std::cerr << "scene textures: failed to load " << relative_path << '\n';
        return false;
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
    };

    const Mapping mappings[] = {
        {SceneTextureKind::Grass, "tiles", "grass"},
        {SceneTextureKind::GrassVariant, "tiles", "grass_variant"},
        {SceneTextureKind::Dirt, "tiles", "dirt"},
        {SceneTextureKind::DirtVariant, "tiles", "dirt_variant"},
        {SceneTextureKind::ForestTree, "tiles", "forest_tree"},
        {SceneTextureKind::ForestTreeAlt, "tiles", "forest_tree_alt"},
        {SceneTextureKind::ForestStump, "tiles", "forest_stump"},
        {SceneTextureKind::Berries, "tiles", "berries"},
        {SceneTextureKind::Blueberries, "tiles", "blueberries"},
        {SceneTextureKind::GoldMine0, "tiles", "gold_mine_0"},
        {SceneTextureKind::GoldMine1, "tiles", "gold_mine_1"},
        {SceneTextureKind::GoldMine2, "tiles", "gold_mine_2"},
        {SceneTextureKind::GoldMine3, "tiles", "gold_mine_3"},
        {SceneTextureKind::TownCenterFriendly, "buildings", "town_center_friendly"},
        {SceneTextureKind::TownCenterEnemy, "buildings", "town_center_enemy"},
        {SceneTextureKind::HouseFriendly, "buildings", "house_friendly"},
        {SceneTextureKind::HouseEnemy, "buildings", "house_enemy"},
    };

    int loaded_count = 0;
    for (const Mapping& mapping : mappings) {
        const std::string relative_path = json_string_at(manifest, mapping.section, mapping.key);
        if (load_texture_file(
                assets_directory,
                relative_path,
                textures_[static_cast<std::size_t>(mapping.kind)])) {
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
