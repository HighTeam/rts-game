// Packs runtime assets into assets.dat (AOA1). Usage:
//   aoa_pack_assets <source_assets_dir> <output_pack_path>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t ASSET_PACK_MAGIC = 0x31414F41U; // 'AOA1' LE
constexpr std::uint16_t ASSET_PACK_VERSION = 1U;

constexpr const char* CURSOR_SHAPE_FOLDERS[] = {
    "01", "02", "04", "05", "13", "20", "21", "22", "23", "27", "28",
};

[[nodiscard]] std::string normalize_path(std::string path)
{
    for (char& character : path) {
        if (character == '\\') {
            character = '/';
        }
    }
    return path;
}

template <typename T>
void write_pod(std::ofstream& output, const T& value)
{
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

[[nodiscard]] bool read_file_bytes(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& out_bytes)
{
    std::ifstream input{path, std::ios::binary};
    if (!input.is_open()) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    out_bytes.resize(size);
    if (size > 0U
        && !input.read(reinterpret_cast<char*>(out_bytes.data()), static_cast<std::streamsize>(size))) {
        return false;
    }

    return true;
}

void collect_data_paths(
    const std::filesystem::path& source_data,
    std::set<std::string>& relative_paths)
{
    if (!std::filesystem::is_directory(source_data)) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_data)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::filesystem::path relative = std::filesystem::relative(entry.path(), source_data);
        relative_paths.insert(
            normalize_path((std::filesystem::path("data") / relative).string()));
    }
}

void collect_cursor_paths(
    const std::filesystem::path& source_assets,
    std::set<std::string>& relative_paths)
{
    for (const char* shape_folder : CURSOR_SHAPE_FOLDERS) {
        const std::filesystem::path cursor_dir =
            source_assets / "textures" / "CursorCrystal" / "PNG" / shape_folder;
        if (!std::filesystem::is_directory(cursor_dir)) {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(cursor_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".png") {
                continue;
            }

            const std::string relative = normalize_path(
                (std::filesystem::path("textures") / "CursorCrystal" / "PNG" / shape_folder
                    / entry.path().filename())
                    .string());
            relative_paths.insert(relative);
        }
    }
}

[[nodiscard]] bool collect_manifest_paths(
    const std::filesystem::path& source_assets,
    std::set<std::string>& relative_paths)
{
    const std::filesystem::path manifest_path = source_assets / "visuals.json";
    std::ifstream input{manifest_path};
    if (!input.is_open()) {
        std::cerr << "pack_assets: missing " << manifest_path.string() << '\n';
        return false;
    }

    const std::string json(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());

    static const std::regex media_path_regex(
        "\"([^\"]+\\.(?:png|PNG|jpg|JPG|jpeg|JPEG|webp|WEBP|bmp|BMP|wav|WAV|ogg|OGG|flac|FLAC))\"");

    for (std::sregex_iterator it(json.begin(), json.end(), media_path_regex), end; it != end; ++it) {
        relative_paths.insert(normalize_path((*it)[1].str()));
    }

    if (relative_paths.empty()) {
        std::cerr << "pack_assets: no media paths found in visuals.json\n";
        return false;
    }

    relative_paths.insert("visuals.json");
    collect_cursor_paths(source_assets, relative_paths);
    return true;
}

struct PackEntry {
    std::string relative_path;
    std::vector<std::uint8_t> bytes;
};

[[nodiscard]] bool build_entries(
    const std::filesystem::path& source_assets,
    const std::filesystem::path& source_data,
    const std::set<std::string>& relative_paths,
    std::vector<PackEntry>& out_entries)
{
    out_entries.clear();
    out_entries.reserve(relative_paths.size());

    for (const std::string& relative : relative_paths) {
        const bool is_data = relative.rfind("data/", 0) == 0;
        const std::filesystem::path source_file = is_data
            ? source_data / std::filesystem::path(relative.substr(5))
            : source_assets / std::filesystem::path(relative);
        PackEntry entry{};
        entry.relative_path = relative;
        if (!read_file_bytes(source_file, entry.bytes)) {
            std::cerr << "pack_assets: failed to read " << source_file.string() << '\n';
            return false;
        }

        out_entries.push_back(std::move(entry));
    }

    return true;
}

[[nodiscard]] bool write_pack(
    const std::filesystem::path& output_path,
    const std::vector<PackEntry>& entries)
{
    std::uint64_t header_size = sizeof(std::uint32_t) + sizeof(std::uint16_t) * 2U + sizeof(std::uint32_t);
    for (const PackEntry& entry : entries) {
        if (entry.relative_path.size() > 0xFFFFU) {
            std::cerr << "pack_assets: path too long: " << entry.relative_path << '\n';
            return false;
        }

        header_size += sizeof(std::uint16_t) + entry.relative_path.size() + sizeof(std::uint64_t) * 2U;
    }

    std::ofstream output{output_path, std::ios::binary | std::ios::trunc};
    if (!output.is_open()) {
        std::cerr << "pack_assets: failed to open output " << output_path.string() << '\n';
        return false;
    }

    write_pod(output, ASSET_PACK_MAGIC);
    write_pod(output, ASSET_PACK_VERSION);
    write_pod(output, static_cast<std::uint16_t>(0U));
    write_pod(output, static_cast<std::uint32_t>(entries.size()));

    std::uint64_t blob_offset = header_size;
    for (const PackEntry& entry : entries) {
        const auto path_length = static_cast<std::uint16_t>(entry.relative_path.size());
        write_pod(output, path_length);
        output.write(entry.relative_path.data(), static_cast<std::streamsize>(path_length));
        write_pod(output, blob_offset);
        write_pod(output, static_cast<std::uint64_t>(entry.bytes.size()));
        blob_offset += entry.bytes.size();
    }

    for (const PackEntry& entry : entries) {
        if (!entry.bytes.empty()) {
            output.write(
                reinterpret_cast<const char*>(entry.bytes.data()),
                static_cast<std::streamsize>(entry.bytes.size()));
        }
    }

    return static_cast<bool>(output);
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc != 4) {
        std::cerr
            << "Usage: aoa_pack_assets <source_assets_dir> <source_data_dir> <output_pack_path>\n";
        return 1;
    }

    const std::filesystem::path source_assets{argv[1]};
    const std::filesystem::path source_data{argv[2]};
    const std::filesystem::path output_path{argv[3]};
    if (!std::filesystem::is_directory(source_assets)) {
        std::cerr << "pack_assets: source assets directory missing: " << source_assets.string()
                  << '\n';
        return 1;
    }

    if (!std::filesystem::is_directory(source_data)) {
        std::cerr << "pack_assets: source data directory missing: " << source_data.string() << '\n';
        return 1;
    }

    std::set<std::string> relative_paths{};
    if (!collect_manifest_paths(source_assets, relative_paths)) {
        return 1;
    }

    collect_data_paths(source_data, relative_paths);

    std::vector<PackEntry> entries{};
    if (!build_entries(source_assets, source_data, relative_paths, entries)) {
        return 1;
    }

    std::filesystem::create_directories(output_path.parent_path());
    if (!write_pack(output_path, entries)) {
        return 1;
    }

    std::cout << "pack_assets: wrote " << entries.size() << " entries -> " << output_path.string()
              << '\n';
    return 0;
}
