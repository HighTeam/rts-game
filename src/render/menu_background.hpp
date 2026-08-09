#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace aoa::render {

// Randomized full-bleed slideshow shown behind the main menu panels.
class MenuBackground {
public:
    MenuBackground() = default;
    ~MenuBackground();

    MenuBackground(const MenuBackground&) = delete;
    MenuBackground& operator=(const MenuBackground&) = delete;

    void load(const std::filesystem::path& assets_directory);
    void destroy_gl_resources();
    void update(float delta_seconds);

    [[nodiscard]] bool ready() const { return !slides_.empty(); }
    [[nodiscard]] unsigned int current_texture_id() const;
    [[nodiscard]] float current_aspect_ratio() const;
    [[nodiscard]] unsigned int incoming_texture_id() const;
    [[nodiscard]] float incoming_aspect_ratio() const;
    [[nodiscard]] float incoming_alpha() const;

private:
    struct Slide {
        unsigned int gl_texture_id{0U};
        float aspect_ratio{1.0F};
    };

    void shuffle_order();
    [[nodiscard]] const Slide* slide_at_order(std::size_t order_index) const;

    std::vector<Slide> slides_{};
    std::vector<std::size_t> order_{};
    std::size_t order_index_{0U};
    float elapsed_seconds_{0.0F};
    unsigned int random_state_{1U};
};

} // namespace aoa::render
