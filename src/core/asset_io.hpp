#pragma once

#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Graphics/Image.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aoa::core {

// Reads an asset into a heap buffer (works for both pack and loose modes).
[[nodiscard]] std::optional<std::vector<std::byte>> read_asset_bytes(std::string_view relative_path);

[[nodiscard]] bool load_image_asset(sf::Image& image, std::string_view relative_path);

[[nodiscard]] bool load_sound_buffer_asset(sf::SoundBuffer& buffer, std::string_view relative_path);

// Opens music from pack memory (zero-copy) or loose file. Pack memory stays owned by AssetStore.
[[nodiscard]] bool open_music_asset(sf::Music& music, std::string_view relative_path);

} // namespace aoa::core
