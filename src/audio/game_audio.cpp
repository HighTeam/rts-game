#include "audio/game_audio.hpp"

#include "core/asset_io.hpp"
#include "core/asset_store.hpp"
#include "core/constants.hpp"
#include "render/game_renderer.hpp"
#include "sim/components/fog_of_war.hpp"
#include "sim/components/tags.hpp"
#include "sim/systems/visibility_system.hpp"

#include <SFML/Audio/SoundSource.hpp>
#include <SFML/System/Vector2.hpp>

#include <algorithm>

namespace aoa::audio {

namespace {

[[nodiscard]] std::string_view sfx_relative_path(const SfxId id)
{
    switch (id) {
    case SfxId::SwordClash:
        return constants::SFX_SWORD_CLASH_RELATIVE_PATH;
    case SfxId::Kick:
        return constants::SFX_KICK_RELATIVE_PATH;
    case SfxId::WoodenClick:
        return constants::SFX_WOODEN_CLICK_RELATIVE_PATH;
    case SfxId::Building:
        return constants::SFX_BUILDING_RELATIVE_PATH;
    case SfxId::Chopping:
        return constants::SFX_CHOPPING_RELATIVE_PATH;
    case SfxId::Gathering:
        return constants::SFX_GATHERING_RELATIVE_PATH;
    case SfxId::Mining:
        return constants::SFX_MINING_RELATIVE_PATH;
    case SfxId::Death1:
        return constants::SFX_DEATH_1_RELATIVE_PATH;
    case SfxId::Death2:
        return constants::SFX_DEATH_2_RELATIVE_PATH;
    case SfxId::Death3:
        return constants::SFX_DEATH_3_RELATIVE_PATH;
    case SfxId::Earth:
        return constants::SFX_EARTH_RELATIVE_PATH;
    case SfxId::What:
        return constants::SFX_WHAT_RELATIVE_PATH;
    case SfxId::Yes:
        return constants::SFX_YES_RELATIVE_PATH;
    case SfxId::No:
        return constants::SFX_NO_RELATIVE_PATH;
    case SfxId::Okay:
        return constants::SFX_OKAY_RELATIVE_PATH;
    case SfxId::IllDoIt:
        return constants::SFX_ILL_DO_IT_RELATIVE_PATH;
    case SfxId::MovingHere:
        return constants::SFX_MOVING_HERE_RELATIVE_PATH;
    case SfxId::Count:
        break;
    }
    return {};
}

[[nodiscard]] std::uint32_t next_random(std::uint32_t& state)
{
    std::uint32_t x = state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    state = x == 0U ? 1U : x;
    return state;
}

[[nodiscard]] float clamp_volume(const float volume_0_to_100)
{
    return std::clamp(volume_0_to_100, constants::AUDIO_VOLUME_MIN, constants::AUDIO_VOLUME_MAX);
}

} // namespace

float GameAudio::effective_sfx_volume() const
{
    return (master_volume_ / constants::AUDIO_VOLUME_MAX)
        * (sfx_volume_ / constants::AUDIO_VOLUME_MAX) * constants::AUDIO_VOLUME_MAX;
}

float GameAudio::effective_music_volume() const
{
    return (master_volume_ / constants::AUDIO_VOLUME_MAX)
        * (music_volume_ / constants::AUDIO_VOLUME_MAX) * constants::AUDIO_VOLUME_MAX;
}

void GameAudio::apply_music_volume()
{
    music_.setVolume(effective_music_volume());
}

void GameAudio::set_master_volume(const float volume_0_to_100)
{
    master_volume_ = clamp_volume(volume_0_to_100);
    apply_music_volume();
    for (auto& voice : active_voices_) {
        if (voice != nullptr) {
            voice->setVolume(effective_sfx_volume());
        }
    }
}

void GameAudio::set_music_volume(const float volume_0_to_100)
{
    music_volume_ = clamp_volume(volume_0_to_100);
    apply_music_volume();
}

void GameAudio::set_sfx_volume(const float volume_0_to_100)
{
    sfx_volume_ = clamp_volume(volume_0_to_100);
    for (auto& voice : active_voices_) {
        if (voice != nullptr) {
            voice->setVolume(effective_sfx_volume());
        }
    }
}

bool GameAudio::load(const std::filesystem::path& assets_directory, const MusicMode music_mode)
{
    (void)assets_directory;
    master_volume_ = constants::AUDIO_MASTER_VOLUME;
    music_volume_ = constants::AUDIO_MUSIC_VOLUME;
    sfx_volume_ = constants::AUDIO_SFX_VOLUME;

    bool any_loaded = false;
    for (std::size_t index = 0U; index < sfx_buffers_.size(); ++index) {
        const SfxId id = static_cast<SfxId>(index);
        const std::string_view relative = sfx_relative_path(id);
        sfx_loaded_[index] = !relative.empty() && core::load_sound_buffer_asset(sfx_buffers_[index], relative);
        any_loaded = any_loaded || sfx_loaded_[index];
    }

    music_mode_ = music_mode;
    if (music_mode_ == MusicMode::MainMenuTheme) {
        music_paths_ = {std::string{constants::MUSIC_MAIN_MENU_RELATIVE_PATH}};
    }
    else {
        music_paths_ = {
            std::string{constants::MUSIC_TRACK_1_RELATIVE_PATH},
            std::string{constants::MUSIC_TRACK_2_RELATIVE_PATH},
            std::string{constants::MUSIC_TRACK_3_RELATIVE_PATH},
        };
    }
    music_track_index_ = 0;
    music_ready_ = false;
    for (const std::string& path : music_paths_) {
        if (core::AssetStore::instance().contains(path)) {
            music_ready_ = true;
            break;
        }
    }

    if (music_ready_) {
        play_next_music_track();
    }

    return any_loaded || music_ready_;
}

void GameAudio::prune_finished_voices()
{
    active_voices_.erase(
        std::remove_if(
            active_voices_.begin(),
            active_voices_.end(),
            [](const std::unique_ptr<sf::Sound>& voice) {
                return voice == nullptr
                    || voice->getStatus() == sf::SoundSource::Status::Stopped;
            }),
        active_voices_.end());
}

void GameAudio::play_sfx(const SfxId id)
{
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= sfx_buffers_.size() || !sfx_loaded_[index]) {
        return;
    }

    prune_finished_voices();
    if (active_voices_.size() >= static_cast<std::size_t>(constants::AUDIO_MAX_CONCURRENT_SFX)) {
        return;
    }

    auto voice = std::make_unique<sf::Sound>(sfx_buffers_[index]);
    voice->setVolume(effective_sfx_volume());
    voice->play();
    active_voices_.push_back(std::move(voice));
}

void GameAudio::play_wooden_click()
{
    play_sfx(SfxId::WoodenClick);
}

void GameAudio::play_random_death()
{
    static constexpr SfxId deaths[] = {SfxId::Death1, SfxId::Death2, SfxId::Death3};
    const std::uint32_t index =
        next_random(random_state_) % static_cast<std::uint32_t>(constants::AUDIO_DEATH_VARIANT_COUNT);
    play_sfx(deaths[index]);
}

void GameAudio::play_random_select_ack()
{
    static constexpr SfxId acks[] = {SfxId::Earth, SfxId::What, SfxId::Yes};
    const std::uint32_t index = next_random(random_state_)
        % static_cast<std::uint32_t>(constants::AUDIO_SELECT_ACK_VARIANT_COUNT);
    play_sfx(acks[index]);
}

void GameAudio::play_random_move_ack()
{
    static constexpr SfxId acks[] = {SfxId::Okay, SfxId::IllDoIt, SfxId::MovingHere};
    const std::uint32_t index = next_random(random_state_)
        % static_cast<std::uint32_t>(constants::AUDIO_MOVE_ACK_VARIANT_COUNT);
    play_sfx(acks[index]);
}

void GameAudio::play_chat_reaction(const std::string& text)
{
    SfxId id = SfxId::Count;
    if (text == constants::CHAT_SFX_YES_TEXT) {
        id = SfxId::Yes;
    }
    else if (text == constants::CHAT_SFX_NO_TEXT) {
        id = SfxId::No;
    }
    else {
        return;
    }

    std::lock_guard lock(pending_chat_reactions_mutex_);
    pending_chat_reactions_.push_back(id);
}

void GameAudio::drain_pending_chat_reactions()
{
    std::vector<SfxId> pending{};
    {
        std::lock_guard lock(pending_chat_reactions_mutex_);
        pending.swap(pending_chat_reactions_);
    }

    for (const SfxId id : pending) {
        play_sfx(id);
    }
}

namespace {

[[nodiscard]] bool sfx_event_is_locally_audible(
    const entt::registry& registry,
    const render::GameRenderer& renderer,
    const std::uint8_t local_player_slot,
    const core::GridPos cell)
{
    const sf::Vector2u window_size = renderer.window_size();
    if (window_size.x == 0U || window_size.y == 0U) {
        return false;
    }

    const sf::Vector2f screen = renderer.tile_center_screen(
        cell.x,
        cell.y,
        constants::RENDER_ENTITY_BASE_LIFT);
    if (screen.x < 0.0F || screen.y < 0.0F
        || screen.x >= static_cast<float>(window_size.x)
        || screen.y >= static_cast<float>(window_size.y)) {
        return false;
    }

    if (!renderer.fog_of_war_enabled()) {
        return true;
    }

    const auto world_view = registry.view<sim::components::WorldTag, sim::components::FogOfWarState>();
    if (world_view.begin() == world_view.end()) {
        return true;
    }

    const auto& fog = world_view.get<sim::components::FogOfWarState>(*world_view.begin());
    if (fog.visible.empty()) {
        return true;
    }

    return sim::systems::is_cell_visible_to_slot(fog, cell, local_player_slot);
}

} // namespace

void GameAudio::drain_sim_sfx(
    entt::registry& registry,
    const render::GameRenderer& renderer,
    const std::uint8_t local_player_slot)
{
    const std::vector<sim::components::SfxEvent> events =
        sim::components::drain_sfx_events(registry);
    for (const sim::components::SfxEvent& event : events) {
        if (!sfx_event_is_locally_audible(registry, renderer, local_player_slot, event.cell)) {
            continue;
        }

        switch (event.kind) {
        case sim::components::SfxEventKind::MilitiaMeleeHit:
            play_sfx(SfxId::SwordClash);
            break;
        case sim::components::SfxEventKind::WorkerMeleeHit:
            play_sfx(SfxId::Kick);
            break;
        case sim::components::SfxEventKind::UnitDeath:
            play_random_death();
            break;
        }
    }
}

void GameAudio::play_next_music_track()
{
    if (!music_ready_) {
        return;
    }

    for (int attempt = 0; attempt < static_cast<int>(music_paths_.size()); ++attempt) {
        const std::string& path = music_paths_[static_cast<std::size_t>(music_track_index_)];
        music_track_index_ = (music_track_index_ + 1) % static_cast<int>(music_paths_.size());
        if (!core::AssetStore::instance().contains(path)) {
            continue;
        }

        if (!core::open_music_asset(music_, path)) {
            continue;
        }

        music_.setLooping(music_mode_ == MusicMode::MainMenuTheme);
        apply_music_volume();
        music_.play();
        return;
    }
}

void GameAudio::update()
{
    drain_pending_chat_reactions();
    prune_finished_voices();
    if (!music_ready_) {
        return;
    }

    if (music_.getStatus() == sf::SoundSource::Status::Stopped) {
        play_next_music_track();
    }
}

} // namespace aoa::audio
