#include "net/lockstep_debug_log.hpp"

#include "core/runtime_paths.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace aoa::net {

namespace {

std::mutex log_mutex{};
std::ofstream log_file{};
bool logging_enabled = false;
std::chrono::steady_clock::time_point last_flush{};
constexpr auto kLogFlushInterval = std::chrono::milliseconds(1000);

const char* role_label(const LockstepRole role)
{
    return role == LockstepRole::Host ? "host" : "client";
}

} // namespace

void LockstepDebugLog::enable(const std::uint8_t player_slot, const LockstepRole role)
{
    std::lock_guard lock(log_mutex);

    if (log_file.is_open()) {
        log_file.close();
    }

    std::filesystem::create_directories(core::default_logs_directory());
    const std::filesystem::path path =
        core::default_logs_directory()
        / ("lockstep_p" + std::to_string(static_cast<int>(player_slot + 1U)) + "_"
           + role_label(role) + ".log");

    log_file.open(path, std::ios::out | std::ios::trunc);
    logging_enabled = log_file.is_open();

    if (!logging_enabled) {
        std::cerr << "lockstep-debug: failed to open " << path.string() << '\n';
        return;
    }

    log_file << timestamp_now() << " debug enabled path=" << path.string() << '\n';
    last_flush = std::chrono::steady_clock::now();
    log_file.flush();
    std::cout << "lockstep-debug: writing to " << path.string() << '\n';
}

void LockstepDebugLog::disable()
{
    std::lock_guard lock(log_mutex);

    if (log_file.is_open()) {
        log_file << timestamp_now() << " debug disabled\n";
        log_file.flush();
        log_file.close();
    }

    logging_enabled = false;
}

bool LockstepDebugLog::is_enabled()
{
    std::lock_guard lock(log_mutex);
    return logging_enabled;
}

std::string LockstepDebugLog::timestamp_now()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto time = clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm local_time{};
    localtime_s(&local_time, &time);

    std::ostringstream formatted{};
    formatted << std::put_time(&local_time, "%H:%M:%S") << '.'
              << std::setfill('0') << std::setw(3) << ms.count();
    return formatted.str();
}

void LockstepDebugLog::log(const std::string_view message)
{
    std::lock_guard lock(log_mutex);

    if (!logging_enabled || !log_file.is_open()) {
        return;
    }

    log_file << timestamp_now() << ' ' << message << '\n';

    const auto now = std::chrono::steady_clock::now();
    if (now - last_flush >= kLogFlushInterval) {
        last_flush = now;
        log_file.flush();
    }
}

void LockstepDebugLog::log_event(const std::string_view event, const std::string_view detail)
{
    if (detail.empty()) {
        log(event);
        return;
    }

    log(std::string(event) + " | " + std::string(detail));
}

} // namespace aoa::net
