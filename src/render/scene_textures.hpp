#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aoa::render {

enum class SceneTextureKind : std::uint8_t {
    Grass = 0,
    GrassVariant,
    Dirt,
    DirtVariant,
    Snow,
    Sand,
    OakForestSmall,
    OakForestMedium,
    OakForestLarge,
    DarkenedOakForestSmall,
    DarkenedOakForestMedium,
    DarkenedOakForestLarge,
    PinesForestSmall,
    PinesForestMedium,
    PinesForestLarge,
    DarkenedPinesForestSmall,
    DarkenedPinesForestMedium,
    DarkenedPinesForestLarge,
    ForestStump,
    Berries,
    Blueberries,
    GoldMine0,
    GoldMine1,
    GoldMine2,
    GoldMine3,
    TownCenter,
    TownCenterMask,
    HouseA,
    HouseAMask,
    HouseB,
    HouseBMask,
    HouseC,
    HouseCMask,
    LumberCamp,
    LumberCampMask,
    Mill,
    MillMask,
    MiningCamp,
    MiningCampMask,
    Barracks,
    BarracksMask,
    MageAcademy,
    MageAcademyMask,
    Tower,
    TowerMask,
    Market,
    MarketMask,
    Extractor,
    ExtractorMask,
    Garden,
    GardenMask,
    Reservoir,
    ReservoirMask,
    Farm,
    FarmDepleted,
    FarmDepletedMask,
    ManaLake,
    Count,
};

class SceneTextureCatalog {
public:
    SceneTextureCatalog();
    ~SceneTextureCatalog();

    SceneTextureCatalog(const SceneTextureCatalog&) = delete;
    SceneTextureCatalog& operator=(const SceneTextureCatalog&) = delete;

    void load(const std::filesystem::path& assets_directory);
    void destroy_gl_resources();

    [[nodiscard]] bool is_loaded() const;
    [[nodiscard]] unsigned int texture_id(const SceneTextureKind kind) const;
    [[nodiscard]] float aspect_ratio(const SceneTextureKind kind) const;

private:
    struct TextureEntry {
        unsigned int gl_texture_id{0U};
        int width{0};
        int height{0};
        bool valid{false};
    };

    [[nodiscard]] bool load_texture_file(
        const std::filesystem::path& assets_directory,
        const std::string& relative_path,
        TextureEntry& entry,
        int crop_x = 0,
        int crop_y = 0,
        int crop_width = 0,
        int crop_height = 0);

    std::vector<TextureEntry> textures_{static_cast<std::size_t>(SceneTextureKind::Count)};
    bool loaded_{false};
};

} // namespace aoa::render
