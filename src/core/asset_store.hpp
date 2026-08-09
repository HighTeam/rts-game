#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aoa::core {

// Runtime asset lookup. Prefers a packed assets.dat; optional loose assets/ + data/ for development.
class AssetStore {
public:
    [[nodiscard]] static AssetStore& instance();

    // Opens assets.dat next to the executable (or AOA_RUNTIME_ROOT). Returns false on failure.
    bool open_pack();

    // Opens loose assets/ + data/ directories (used by --loose-assets).
    bool open_loose_directories(
        const std::filesystem::path& assets_directory,
        const std::filesystem::path& data_directory);

    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] bool is_loose() const { return loose_; }
    [[nodiscard]] const std::filesystem::path& loose_root() const { return loose_assets_root_; }

    [[nodiscard]] bool contains(std::string_view relative_path) const;

    // Keys with the given prefix (normalized, typically ending with '/').
    [[nodiscard]] std::vector<std::string> list_prefix(std::string_view prefix) const;

    // Relative path using forward slashes, matching visuals.json / data/ pack keys.
    // Pack mode only (zero-copy into the loaded pack buffer).
    [[nodiscard]] std::optional<std::span<const std::byte>> find(std::string_view relative_path) const;

    // Convenience: UTF-8 / binary files into a new buffer (pack or loose).
    [[nodiscard]] std::optional<std::vector<std::byte>> read_bytes(std::string_view relative_path) const;

    // Convenience: UTF-8 text files (e.g. visuals.json).
    [[nodiscard]] std::optional<std::string> read_text(std::string_view relative_path) const;

private:
    struct PackedEntry {
        std::uint64_t offset{0U};
        std::uint64_t size{0U};
    };

    [[nodiscard]] std::filesystem::path resolve_loose_file_path(std::string_view relative_path) const;

    bool ready_{false};
    bool loose_{false};
    std::filesystem::path loose_assets_root_{};
    std::filesystem::path loose_data_root_{};
    std::vector<std::byte> pack_bytes_{};
    std::unordered_map<std::string, PackedEntry> entries_{};
};

// Normalize path separators to '/' for stable pack keys.
[[nodiscard]] std::string normalize_asset_path(std::string_view relative_path);

// Initialize global store: loose directories when requested, else assets.dat (with loose fallback).
[[nodiscard]] bool init_asset_store(bool prefer_loose_assets);

} // namespace aoa::core
