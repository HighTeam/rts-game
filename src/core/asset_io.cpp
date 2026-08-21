#include "core/asset_io.hpp"

#include "core/asset_store.hpp"

#include <filesystem>

namespace aoa::core {

std::optional<std::vector<std::byte>> read_asset_bytes(const std::string_view relative_path)
{
    return AssetStore::instance().read_bytes(relative_path);
}

bool load_image_asset(sf::Image& image, const std::string_view relative_path)
{
    AssetStore& store = AssetStore::instance();
    if (!store.ready()) {
        return false;
    }

    const std::string key = normalize_asset_path(relative_path);
    if (!store.is_loose()) {
        const auto span = store.find(key);
        if (!span.has_value()) {
            return false;
        }

        return image.loadFromMemory(span->data(), span->size());
    }

    const auto bytes = store.read_bytes(key);
    if (!bytes.has_value()) {
        return false;
    }

    return image.loadFromMemory(bytes->data(), bytes->size());
}

bool load_sound_buffer_asset(sf::SoundBuffer& buffer, const std::string_view relative_path)
{
    AssetStore& store = AssetStore::instance();
    if (!store.ready()) {
        return false;
    }

    const std::string key = normalize_asset_path(relative_path);
    if (!store.is_loose()) {
        const auto span = store.find(key);
        if (!span.has_value()) {
            return false;
        }

        return buffer.loadFromMemory(span->data(), span->size());
    }

    const auto bytes = store.read_bytes(key);
    if (!bytes.has_value()) {
        return false;
    }

    return buffer.loadFromMemory(bytes->data(), bytes->size());
}

bool open_music_asset(sf::Music& music, const std::string_view relative_path)
{
    AssetStore& store = AssetStore::instance();
    if (!store.ready()) {
        return false;
    }

    const std::string key = normalize_asset_path(relative_path);
    if (!store.is_loose()) {
        const auto span = store.find(key);
        if (!span.has_value()) {
            return false;
        }

        return music.openFromMemory(span->data(), span->size());
    }

    return music.openFromFile(store.loose_root() / std::filesystem::path(key));
}

} // namespace aoa::core
