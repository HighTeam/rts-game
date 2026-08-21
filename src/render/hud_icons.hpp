#pragma once

#include "core/constants.hpp"

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

enum class UnitPortrait : std::uint8_t {
    WorkerMale = 0,
    WorkerFemale = 1,
    MilitiaMale = 2,
    MilitiaFemale = 3,
    MageMale = 4,
    MageFemale = 5,
    Count = 6,
};

enum class CivLogo : std::uint8_t {
    Earth = 0,
    Water = 1,
    Fire = 2,
    Air = 3,
    Count = 4,
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

class UnitPortraitAtlas {
public:
    [[nodiscard]] bool load(const std::filesystem::path& assets_directory);
    void destroy_gl_resources();

    [[nodiscard]] bool ready() const { return texture_id_ != 0U; }
    [[nodiscard]] unsigned int texture_id() const { return texture_id_; }

    void icon_uv(
        UnitPortrait portrait,
        float& u0,
        float& v0,
        float& u1,
        float& v1) const;

private:
    unsigned int texture_id_{0U};
    int atlas_width_{0};
    int atlas_height_{0};
};

class CivLogoAtlas {
public:
    [[nodiscard]] bool load(const std::filesystem::path& assets_directory);
    void destroy_gl_resources();

    [[nodiscard]] bool ready() const { return texture_id_ != 0U; }
    [[nodiscard]] unsigned int texture_id() const { return texture_id_; }

    void icon_uv(
        CivLogo logo,
        float& u0,
        float& v0,
        float& u1,
        float& v1) const;

private:
    unsigned int texture_id_{0U};
    int atlas_width_{0};
    int atlas_height_{0};
};

[[nodiscard]] inline UnitPortrait unit_portrait_for(
    const bool is_worker,
    const bool is_militia,
    const bool is_mage,
    const constants::UnitSex sex)
{
    const bool female = sex == constants::UnitSex::Female;
    if (is_mage) {
        return female ? UnitPortrait::MageFemale : UnitPortrait::MageMale;
    }
    if (is_militia) {
        return female ? UnitPortrait::MilitiaFemale : UnitPortrait::MilitiaMale;
    }
    if (is_worker) {
        return female ? UnitPortrait::WorkerFemale : UnitPortrait::WorkerMale;
    }

    return UnitPortrait::Count;
}

[[nodiscard]] inline CivLogo civ_logo_for(const constants::Civilization civilization)
{
    switch (civilization) {
    case constants::Civilization::Water:
        return CivLogo::Water;
    case constants::Civilization::Fire:
        return CivLogo::Fire;
    case constants::Civilization::Air:
        return CivLogo::Air;
    case constants::Civilization::Earth:
    default:
        return CivLogo::Earth;
    }
}

} // namespace aoa::render
