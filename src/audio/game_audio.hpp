#pragma once

#include "sim/components/sfx_events.hpp"

#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace aoa::render {
class GameRenderer;
}

namespace aoa::audio {

enum class SfxId : std::uint8_t {
    SwordClash = 0,
    Kick = 1,
    WoodenClick = 2,
    Building = 3,
    Chopping = 4,
    Gathering = 5,
    Mining = 6,
    Death1 = 7,
    Death2 = 8,
    Death3 = 9,
    Earth = 10,
    What = 11,
    Yes = 12,
    No = 13,
    Okay = 14,
    IllDoIt = 15,
    MovingHere = 16,
    Count = 17,
};

enum class MusicMode : std::uint8_t {
    GameTracks = 0,
    MainMenuTheme = 1,
};

class GameAudio {
public:
    bool load(
        const std::filesystem::path& assets_directory,
        MusicMode music_mode = MusicMode::GameTracks);
    void update();
    void play_sfx(SfxId id);
    void play_wooden_click();
    void play_random_death();
    void play_random_select_ack();
    void play_random_move_ack();
    /// Thread-safe: queues Yes/No reactions for playback on the audio update thread.
    void play_chat_reaction(const std::string& text);
    void drain_sim_sfx(
        entt::registry& registry,
        const render::GameRenderer& renderer,
        std::uint8_t local_player_slot);

    void set_master_volume(float volume_0_to_100);
    void set_music_volume(float volume_0_to_100);
    void set_sfx_volume(float volume_0_to_100);
    [[nodiscard]] float master_volume() const { return master_volume_; }
    [[nodiscard]] float music_volume() const { return music_volume_; }
    [[nodiscard]] float sfx_volume() const { return sfx_volume_; }

private:
    void play_next_music_track();
    void prune_finished_voices();
    void drain_pending_chat_reactions();
    void apply_music_volume();
    [[nodiscard]] float effective_sfx_volume() const;
    [[nodiscard]] float effective_music_volume() const;

    std::array<sf::SoundBuffer, static_cast<std::size_t>(SfxId::Count)> sfx_buffers_{};
    std::array<bool, static_cast<std::size_t>(SfxId::Count)> sfx_loaded_{};
    std::vector<std::unique_ptr<sf::Sound>> active_voices_{};
    sf::Music music_{};
    std::vector<std::string> music_paths_{};
    MusicMode music_mode_{MusicMode::GameTracks};
    int music_track_index_{0};
    bool music_ready_{false};
    std::uint32_t random_state_{1U};
    float master_volume_{100.0F};
    float music_volume_{45.0F};
    float sfx_volume_{80.0F};
    std::mutex pending_chat_reactions_mutex_{};
    std::vector<SfxId> pending_chat_reactions_{};
};

} // namespace aoa::audio
