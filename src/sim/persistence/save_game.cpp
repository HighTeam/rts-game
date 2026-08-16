#include "sim/persistence/save_game.hpp"

#include "core/runtime_paths.hpp"
#include "sim/snapshot/sim_snapshot.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <system_error>
#include <vector>

namespace aoa::sim::persistence {
namespace {

[[nodiscard]] bool write_bytes_to_file(
    const std::filesystem::path& path,
    const std::vector<std::byte>& bytes)
{
    std::error_code error{};
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    if (!bytes.empty()) {
        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }

    return static_cast<bool>(out);
}

[[nodiscard]] std::optional<std::vector<std::byte>> read_bytes_from_file(
    const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }

    const auto end = in.tellg();
    if (end < 0) {
        return std::nullopt;
    }

    const auto size = static_cast<std::size_t>(end);
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size);
    if (size > 0U) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!in) {
            return std::nullopt;
        }
    }

    return bytes;
}

} // namespace

std::filesystem::path default_saves_directory()
{
    const std::filesystem::path executable_dir = core::executable_directory();
    if (!executable_dir.empty()) {
        return executable_dir / "saves";
    }

    return std::filesystem::path("saves");
}

std::filesystem::path save_path_for_stem(const std::string_view stem)
{
    std::string filename{stem};
    if (filename.size() < SAVE_EXTENSION.size()
        || filename.substr(filename.size() - SAVE_EXTENSION.size()) != SAVE_EXTENSION) {
        filename.append(SAVE_EXTENSION);
    }

    return default_saves_directory() / filename;
}

std::filesystem::path default_autosave_path()
{
    return default_saves_directory() / AUTOSAVE_FILENAME;
}

std::filesystem::path default_autosave_mp_path()
{
    return default_saves_directory() / AUTOSAVE_MP_FILENAME;
}

bool save_simulation_to_file(
    const Simulation& simulation,
    const std::filesystem::path& path)
{
    const std::vector<std::byte> bytes = encode_sim_snapshot(simulation);
    if (bytes.empty()) {
        return false;
    }

    return write_bytes_to_file(path, bytes);
}

bool load_simulation_from_file(Simulation& simulation, const std::filesystem::path& path)
{
    const auto bytes = read_bytes_from_file(path);
    if (!bytes.has_value() || bytes->empty()) {
        return false;
    }

    return simulation.apply_snapshot(*bytes);
}

bool save_file_exists(const std::string_view stem)
{
    std::error_code error{};
    return std::filesystem::is_regular_file(save_path_for_stem(stem), error) && !error;
}

std::vector<std::string> list_save_stems()
{
    std::vector<std::string> stems{};
    std::error_code error{};
    const std::filesystem::path directory = default_saves_directory();
    if (!std::filesystem::is_directory(directory, error) || error) {
        return stems;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;
        }

        if (!entry.is_regular_file(error) || error) {
            continue;
        }

        const std::filesystem::path filename = entry.path().filename();
        const std::string name = filename.string();
        if (name.size() <= SAVE_EXTENSION.size()) {
            continue;
        }

        if (name.substr(name.size() - SAVE_EXTENSION.size()) != SAVE_EXTENSION) {
            continue;
        }

        stems.push_back(name.substr(0U, name.size() - SAVE_EXTENSION.size()));
    }

    std::sort(stems.begin(), stems.end());
    return stems;
}

std::optional<std::string> normalize_save_stem(const std::string_view input)
{
    std::string stem{input};
    while (!stem.empty() && (stem.front() == ' ' || stem.front() == '\t')) {
        stem.erase(stem.begin());
    }
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '\t')) {
        stem.pop_back();
    }

    if (stem.size() >= SAVE_EXTENSION.size()
        && stem.substr(stem.size() - SAVE_EXTENSION.size()) == SAVE_EXTENSION) {
        stem.resize(stem.size() - SAVE_EXTENSION.size());
    }

    if (stem.empty()) {
        return std::nullopt;
    }

    for (const char character : stem) {
        if (character == '/' || character == '\\' || character == ':' || character == '*'
            || character == '?' || character == '"' || character == '<' || character == '>'
            || character == '|') {
            return std::nullopt;
        }
    }

    return stem;
}

} // namespace aoa::sim::persistence
