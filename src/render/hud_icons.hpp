#pragma once

#include <cstdint>
#include <filesystem>

namespace aoa::render {

enum class HudIcon : std::uint8_t {
    Death = 0,
    ArrowSe,
    Firecamp,
    House,
    Sword,
    GreenHat,
    MilitiaHat,
    BlueTShirt,
    HandDown,
    Boots,
    Mana,
    ManaPlus,
    ManaStar,
    Axe,
    Hammer,
    Money,
    MoneyReceive,
    MoneyDeposit,
    Food,
    Wood,
    LumberCamp,
    Mill,
    MiningCamp,
    Map,
    ClosedEye,
    Count,
};

enum class EarthBuildIcon : std::uint8_t {
    House = 0,
    TownCenter = 1,
    Market = 2,
    Barracks = 3,
    Tower = 4,
    MageAcademy = 5,
    Extractor = 6,
    Garden = 7,
    Reservoir = 8,
    Farm = 9,
    Count = 10,
};

class HudIconAtlas {
public:
    [[nodiscard]] bool load(const std::filesystem::path& assets_directory);
    void destroy_gl_resources();

    [[nodiscard]] bool ready() const { return texture_id_ != 0U; }
    [[nodiscard]] unsigned int texture_id() const { return texture_id_; }
    [[nodiscard]] int atlas_width() const { return atlas_width_; }
    [[nodiscard]] int atlas_height() const { return atlas_height_; }

    void icon_uv(
        HudIcon icon,
        float& u0,
        float& v0,
        float& u1,
        float& v1) const;

private:
    unsigned int texture_id_{0U};
    int atlas_width_{0};
    int atlas_height_{0};
};

class EarthBuildIconAtlas {
public:
    [[nodiscard]] bool load(const std::filesystem::path& assets_directory);
    void destroy_gl_resources();

    [[nodiscard]] bool ready() const { return texture_id_ != 0U; }
    [[nodiscard]] unsigned int texture_id() const { return texture_id_; }

    void icon_uv(
        EarthBuildIcon icon,
        float& u0,
        float& v0,
        float& u1,
        float& v1) const;

private:
    unsigned int texture_id_{0U};
    int atlas_width_{0};
    int atlas_height_{0};
};

} // namespace aoa::render
