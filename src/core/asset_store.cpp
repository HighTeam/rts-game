#include "core/asset_store.hpp"

#include "core/asset_pack_format.hpp"
#include "core/runtime_paths.hpp"

#include <cstring>
#include <fstream>
#include <iostream>

namespace aoa::core {

namespace {

template <typename T>
[[nodiscard]] bool read_pod(const std::byte*& cursor, const std::byte* end, T& out)
{
    if (cursor + sizeof(T) > end) {
        return false;
    }

    std::memcpy(&out, cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

[[nodiscard]] std::filesystem::path resolve_pack_path()
{
    const std::filesystem::path exe_dir = executable_directory();
    if (!exe_dir.empty()) {
        const std::filesystem::path beside_exe = exe_dir / ASSET_PACK_FILENAME;
        if (std::filesystem::is_regular_file(beside_exe)) {
            return beside_exe;
        }
    }

#ifdef AOA_RUNTIME_ROOT
    const std::filesystem::path beside_root =
        std::filesystem::path(AOA_RUNTIME_ROOT) / ASSET_PACK_FILENAME;
    if (std::filesystem::is_regular_file(beside_root)) {
        return beside_root;
    }
#endif

    if (!exe_dir.empty()) {
        return exe_dir / ASSET_PACK_FILENAME;
    }

    return std::filesystem::path(ASSET_PACK_FILENAME);
}

[[nodiscard]] bool starts_with(const std::string_view text, const std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] std::string ensure_trailing_slash(std::string prefix)
{
    if (!prefix.empty() && prefix.back() != '/') {
        prefix.push_back('/');
    }
    return prefix;
}

} // namespace

std::string normalize_asset_path(const std::string_view relative_path)
{
    std::string normalized{relative_path};
    for (char& character : normalized) {
        if (character == '\\') {
            character = '/';
        }
    }
    return normalized;
}

AssetStore& AssetStore::instance()
{
    static AssetStore store{};
    return store;
}

std::filesystem::path AssetStore::resolve_loose_file_path(const std::string_view relative_path) const
{
    const std::string key = normalize_asset_path(relative_path);
    if (starts_with(key, DATA_PACK_PREFIX)) {
        const std::string_view under_data = std::string_view{key}.substr(DATA_PACK_PREFIX.size());
        return loose_data_root_ / std::filesystem::path(under_data);
    }

    return loose_assets_root_ / std::filesystem::path(key);
}

bool AssetStore::open_loose_directories(
    const std::filesystem::path& assets_directory,
    const std::filesystem::path& data_directory)
{
    if (!std::filesystem::is_directory(assets_directory)
        || !std::filesystem::is_directory(data_directory)) {
        return false;
    }

    pack_bytes_.clear();
    entries_.clear();
    loose_assets_root_ = assets_directory;
    loose_data_root_ = data_directory;
    loose_ = true;
    ready_ = true;
    return true;
}

bool AssetStore::open_pack()
{
    const std::filesystem::path pack_path = resolve_pack_path();
    std::ifstream input{pack_path, std::ios::binary};
    if (!input.is_open()) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const auto file_size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    if (file_size < sizeof(std::uint32_t) + sizeof(std::uint16_t) * 2U + sizeof(std::uint32_t)) {
        return false;
    }

    pack_bytes_.resize(file_size);
    if (!input.read(reinterpret_cast<char*>(pack_bytes_.data()), static_cast<std::streamsize>(file_size))) {
        pack_bytes_.clear();
        return false;
    }

    const std::byte* cursor = pack_bytes_.data();
    const std::byte* end = pack_bytes_.data() + pack_bytes_.size();

    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    std::uint16_t reserved = 0U;
    std::uint32_t entry_count = 0U;
    if (!read_pod(cursor, end, magic) || magic != ASSET_PACK_MAGIC
        || !read_pod(cursor, end, version) || version != ASSET_PACK_VERSION
        || !read_pod(cursor, end, reserved) || !read_pod(cursor, end, entry_count)) {
        pack_bytes_.clear();
        return false;
    }

    entries_.clear();
    entries_.reserve(entry_count);
    for (std::uint32_t index = 0U; index < entry_count; ++index) {
        std::uint16_t path_length = 0U;
        if (!read_pod(cursor, end, path_length) || path_length == 0U) {
            pack_bytes_.clear();
            entries_.clear();
            return false;
        }

        if (cursor + path_length > end) {
            pack_bytes_.clear();
            entries_.clear();
            return false;
        }

        std::string path(
            reinterpret_cast<const char*>(cursor),
            reinterpret_cast<const char*>(cursor) + path_length);
        cursor += path_length;

        PackedEntry entry{};
        if (!read_pod(cursor, end, entry.offset) || !read_pod(cursor, end, entry.size)) {
            pack_bytes_.clear();
            entries_.clear();
            return false;
        }

        if (entry.offset + entry.size > pack_bytes_.size()) {
            pack_bytes_.clear();
            entries_.clear();
            return false;
        }

        entries_.emplace(normalize_asset_path(path), entry);
    }

    loose_ = false;
    loose_assets_root_.clear();
    loose_data_root_.clear();
    ready_ = true;
    return true;
}

bool AssetStore::contains(const std::string_view relative_path) const
{
    if (!ready_) {
        return false;
    }

    const std::string key = normalize_asset_path(relative_path);
    if (!loose_) {
        return entries_.find(key) != entries_.end();
    }

    return std::filesystem::is_regular_file(resolve_loose_file_path(key));
}

std::vector<std::string> AssetStore::list_prefix(const std::string_view prefix) const
{
    std::vector<std::string> keys{};
    if (!ready_) {
        return keys;
    }

    const std::string normalized_prefix = ensure_trailing_slash(normalize_asset_path(prefix));
    if (!loose_) {
        for (const auto& [key, entry] : entries_) {
            (void)entry;
            if (starts_with(key, normalized_prefix)) {
                keys.push_back(key);
            }
        }
        return keys;
    }

    const bool listing_data = starts_with(normalized_prefix, DATA_PACK_PREFIX);
    const std::filesystem::path scan_root =
        listing_data ? loose_data_root_ : loose_assets_root_;
    const std::string relative_under_root = listing_data
        ? normalized_prefix.substr(DATA_PACK_PREFIX.size())
        : normalized_prefix;

    const std::filesystem::path scan_directory =
        relative_under_root.empty() ? scan_root : scan_root / std::filesystem::path(relative_under_root);
    if (!std::filesystem::is_directory(scan_directory)) {
        return keys;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(scan_directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::filesystem::path relative = std::filesystem::relative(entry.path(), scan_root);
        std::string key = normalize_asset_path(relative.string());
        if (listing_data) {
            key = std::string{DATA_PACK_PREFIX} + key;
        }

        if (starts_with(key, normalized_prefix)) {
            keys.push_back(std::move(key));
        }
    }

    return keys;
}

std::optional<std::span<const std::byte>> AssetStore::find(const std::string_view relative_path) const
{
    if (!ready_ || loose_) {
        return std::nullopt;
    }

    const auto found = entries_.find(normalize_asset_path(relative_path));
    if (found == entries_.end()) {
        return std::nullopt;
    }

    return std::span<const std::byte>(
        pack_bytes_.data() + static_cast<std::size_t>(found->second.offset),
        static_cast<std::size_t>(found->second.size));
}

std::optional<std::vector<std::byte>> AssetStore::read_bytes(const std::string_view relative_path) const
{
    if (!ready_) {
        return std::nullopt;
    }

    const std::string key = normalize_asset_path(relative_path);
    if (!loose_) {
        const auto span = find(key);
        if (!span.has_value()) {
            return std::nullopt;
        }

        return std::vector<std::byte>(span->begin(), span->end());
    }

    const std::filesystem::path file_path = resolve_loose_file_path(key);
    std::ifstream input{file_path, std::ios::binary};
    if (!input.is_open()) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size);
    if (size > 0U
        && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }

    return bytes;
}

std::optional<std::string> AssetStore::read_text(const std::string_view relative_path) const
{
    const auto bytes = read_bytes(relative_path);
    if (!bytes.has_value()) {
        return std::nullopt;
    }

    return std::string(
        reinterpret_cast<const char*>(bytes->data()),
        reinterpret_cast<const char*>(bytes->data()) + bytes->size());
}

bool init_asset_store(const bool prefer_loose_assets)
{
    AssetStore& store = AssetStore::instance();
    const std::filesystem::path assets_directory = default_assets_directory();
    const std::filesystem::path data_directory = default_data_directory();

    if (prefer_loose_assets) {
        if (store.open_loose_directories(assets_directory, data_directory)) {
            std::cout << "assets: using loose directories\n  assets=" << assets_directory.string()
                      << "\n  data=" << data_directory.string() << '\n';
            return true;
        }

        std::cerr << "assets: --loose-assets requested but assets/ or data/ not found\n";
        return false;
    }

    if (store.open_pack()) {
        std::cout << "assets: using pack " << ASSET_PACK_FILENAME << '\n';
        return true;
    }

    if (store.open_loose_directories(assets_directory, data_directory)) {
        std::cout << "assets: pack missing, falling back to loose directories\n  assets="
                  << assets_directory.string() << "\n  data=" << data_directory.string() << '\n';
        return true;
    }

    std::cerr << "assets: failed to open " << ASSET_PACK_FILENAME << " or loose assets/data\n";
    return false;
}

} // namespace aoa::core
