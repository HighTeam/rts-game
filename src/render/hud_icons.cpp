#include "render/hud_icons.hpp"

#include "core/asset_io.hpp"
#include "core/constants.hpp"

#include <glad/glad.h>

#include <SFML/Graphics/Image.hpp>

#include <array>

namespace aoa::render {

namespace {

struct IconTile {
    int row{1};
    int column{1};
};

// Rows/columns are 1-indexed per art pack docs.
constexpr std::array<IconTile, static_cast<std::size_t>(HudIcon::Count)> ICON_TILES{{
    {1, 1},   // Death
    {3, 4},   // ArrowSe
    {5, 3},   // Firecamp
    {5, 4},   // House
    {6, 5},   // Sword
    {8, 1},   // GreenHat
    {8, 3},   // MilitiaHat
    {8, 9},   // BlueTShirt
    {9, 1},   // HandDown
    {9, 3},   // Boots
    {10, 2},  // Mana
    {10, 6},  // ManaPlus
    {10, 10}, // ManaStar
    {11, 2},  // Axe
    {11, 5},  // Hammer
    {13, 12}, // Money
    {13, 13}, // MoneyReceive
    {13, 14}, // MoneyDeposit
    {15, 14}, // Food
    {18, 1},  // Wood
}};

constexpr const char* ATLAS_RELATIVE_PATH =
    "textures/Shikashi's Fantasy Icons Pack/2 - Transparent & Drop Shadow.png";

} // namespace

bool HudIconAtlas::load(const std::filesystem::path& assets_directory)
{
    destroy_gl_resources();
    (void)assets_directory;

    sf::Image image{};
    if (!core::load_image_asset(image, ATLAS_RELATIVE_PATH)) {
        return false;
    }

    const sf::Vector2u size = image.getSize();
    if (size.x == 0U || size.y == 0U) {
        return false;
    }

    // Keep top-left origin for UV math (row/column from art pack).
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
        static_cast<GLsizei>(size.x),
        static_cast<GLsizei>(size.y),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.getPixelsPtr());
    glBindTexture(GL_TEXTURE_2D, 0U);

    texture_id_ = texture_id;
    atlas_width_ = static_cast<int>(size.x);
    atlas_height_ = static_cast<int>(size.y);
    return true;
}

void HudIconAtlas::destroy_gl_resources()
{
    if (texture_id_ != 0U) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0U;
    }
    atlas_width_ = 0;
    atlas_height_ = 0;
}

void HudIconAtlas::icon_uv(
    const HudIcon icon,
    float& u0,
    float& v0,
    float& u1,
    float& v1) const
{
    if (!ready() || icon >= HudIcon::Count) {
        u0 = v0 = 0.0F;
        u1 = v1 = 1.0F;
        return;
    }

    const IconTile tile = ICON_TILES[static_cast<std::size_t>(icon)];
    const float tile_size = static_cast<float>(constants::HUD_ICON_TILE_SIZE_PX);
    const float width = static_cast<float>(atlas_width_);
    const float height = static_cast<float>(atlas_height_);
    const float x0 = static_cast<float>(tile.column - 1) * tile_size;
    const float y0 = static_cast<float>(tile.row - 1) * tile_size;
    u0 = x0 / width;
    v0 = y0 / height;
    u1 = (x0 + tile_size) / width;
    v1 = (y0 + tile_size) / height;
}

} // namespace aoa::render
