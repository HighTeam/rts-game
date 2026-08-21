#include "render/menu_background.hpp"

#include "core/asset_io.hpp"
#include "core/constants.hpp"

#include <glad/glad.h>

#include <SFML/Graphics/Image.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>

namespace aoa::render {

namespace {

[[nodiscard]] unsigned int next_random(unsigned int& state)
{
    unsigned int value = state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    state = value == 0U ? 1U : value;
    return state;
}

[[nodiscard]] unsigned int upload_texture(const std::string_view relative_path, float& out_aspect_ratio)
{
    sf::Image image{};
    if (!core::load_image_asset(image, relative_path)) {
        std::cerr << "menu background: failed to load " << relative_path << '\n';
        return 0U;
    }

    const sf::Vector2u size = image.getSize();
    if (size.x == 0U || size.y == 0U) {
        return 0U;
    }

    unsigned int texture_id = 0U;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<int>(size.x),
        static_cast<int>(size.y),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.getPixelsPtr());
    glBindTexture(GL_TEXTURE_2D, 0U);

    out_aspect_ratio = static_cast<float>(size.y) / static_cast<float>(size.x);
    return texture_id;
}

} // namespace

MenuBackground::~MenuBackground()
{
    destroy_gl_resources();
}

void MenuBackground::destroy_gl_resources()
{
    for (Slide& slide : slides_) {
        if (slide.gl_texture_id != 0U) {
            glDeleteTextures(1, &slide.gl_texture_id);
            slide.gl_texture_id = 0U;
        }
    }

    slides_.clear();
    order_.clear();
    order_index_ = 0U;
    elapsed_seconds_ = 0.0F;
}

void MenuBackground::shuffle_order()
{
    order_.resize(slides_.size());
    for (std::size_t index = 0U; index < order_.size(); ++index) {
        order_[index] = index;
    }

    for (std::size_t index = order_.size(); index > 1U; --index) {
        const std::size_t swap_index =
            static_cast<std::size_t>(next_random(random_state_) % static_cast<unsigned int>(index));
        std::swap(order_[index - 1U], order_[swap_index]);
    }
}

void MenuBackground::load(const std::filesystem::path& assets_directory)
{
    destroy_gl_resources();
    (void)assets_directory;

    random_state_ = static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count() | 1);

    for (const std::string_view relative_path : constants::MENU_SLIDESHOW_RELATIVE_PATHS) {
        float aspect_ratio = 1.0F;
        const unsigned int texture_id = upload_texture(relative_path, aspect_ratio);
        if (texture_id == 0U) {
            continue;
        }

        slides_.push_back(Slide{texture_id, aspect_ratio});
    }

    shuffle_order();
    order_index_ = 0U;
    elapsed_seconds_ = 0.0F;
}

const MenuBackground::Slide* MenuBackground::slide_at_order(const std::size_t order_index) const
{
    if (order_.empty()) {
        return nullptr;
    }

    const std::size_t slide_index = order_[order_index % order_.size()];
    if (slide_index >= slides_.size()) {
        return nullptr;
    }

    return &slides_[slide_index];
}

void MenuBackground::update(const float delta_seconds)
{
    if (slides_.size() <= 1U) {
        return;
    }

    elapsed_seconds_ += delta_seconds;
    if (elapsed_seconds_ < constants::MENU_SLIDESHOW_DWELL_SECONDS) {
        return;
    }

    elapsed_seconds_ -= constants::MENU_SLIDESHOW_DWELL_SECONDS;
    ++order_index_;
    if (order_index_ >= order_.size()) {
        shuffle_order();
        order_index_ = 0U;
    }
}

unsigned int MenuBackground::current_texture_id() const
{
    const Slide* slide = slide_at_order(order_index_);
    return slide == nullptr ? 0U : slide->gl_texture_id;
}

float MenuBackground::current_aspect_ratio() const
{
    const Slide* slide = slide_at_order(order_index_);
    return slide == nullptr ? 1.0F : slide->aspect_ratio;
}

unsigned int MenuBackground::incoming_texture_id() const
{
    const Slide* slide = slide_at_order(order_index_ + 1U);
    return slide == nullptr ? 0U : slide->gl_texture_id;
}

float MenuBackground::incoming_aspect_ratio() const
{
    const Slide* slide = slide_at_order(order_index_ + 1U);
    return slide == nullptr ? 1.0F : slide->aspect_ratio;
}

float MenuBackground::incoming_alpha() const
{
    const float fade_start =
        constants::MENU_SLIDESHOW_DWELL_SECONDS - constants::MENU_SLIDESHOW_FADE_SECONDS;
    if (slides_.size() <= 1U || elapsed_seconds_ <= fade_start) {
        return 0.0F;
    }

    return std::clamp(
        (elapsed_seconds_ - fade_start) / constants::MENU_SLIDESHOW_FADE_SECONDS,
        0.0F,
        1.0F);
}

} // namespace aoa::render
