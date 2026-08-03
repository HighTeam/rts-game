#pragma once

#include "core/constants.hpp"

#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace aoa::math {

// Q16.16 fixed-point for deterministic simulation math.
class Fixed {
public:
    Fixed() : raw_(0) {}

    static Fixed from_raw(std::int32_t raw) { return Fixed(raw); }

    static Fixed from_int(std::int32_t value)
    {
        return Fixed(value * constants::FIXED_ONE_RAW);
    }

    static Fixed from_float(float value)
    {
        return Fixed(static_cast<std::int32_t>(std::lround(
            value * static_cast<float>(constants::FIXED_ONE_RAW))));
    }

    [[nodiscard]] std::int32_t raw() const { return raw_; }

    [[nodiscard]] std::int32_t to_int() const
    {
        return raw_ >> constants::FIXED_FRACTION_BITS;
    }

    [[nodiscard]] float to_float() const
    {
        return static_cast<float>(raw_) /
               static_cast<float>(constants::FIXED_ONE_RAW);
    }

    Fixed operator+(Fixed other) const { return Fixed(raw_ + other.raw_); }
    Fixed operator-(Fixed other) const { return Fixed(raw_ - other.raw_); }

    Fixed operator*(Fixed other) const
    {
        const std::int64_t product =
            static_cast<std::int64_t>(raw_) * static_cast<std::int64_t>(other.raw_);
        return Fixed(static_cast<std::int32_t>(product >> constants::FIXED_FRACTION_BITS));
    }

    Fixed operator/(Fixed other) const
    {
        if (other.raw_ == 0) {
            throw std::domain_error("Fixed division by zero");
        }

        const std::int64_t numerator =
            static_cast<std::int64_t>(raw_) << constants::FIXED_FRACTION_BITS;
        return Fixed(static_cast<std::int32_t>(numerator / other.raw_));
    }

    bool operator==(const Fixed& other) const
    {
        return raw_ == other.raw_;
    }

    bool operator!=(const Fixed& other) const
    {
        return raw_ != other.raw_;
    }

    bool operator<(const Fixed& other) const
    {
        return raw_ < other.raw_;
    }

    bool operator>(const Fixed& other) const
    {
        return raw_ > other.raw_;
    }

    bool operator<=(const Fixed& other) const
    {
        return raw_ <= other.raw_;
    }

    bool operator>=(const Fixed& other) const
    {
        return raw_ >= other.raw_;
    }

private:
    explicit Fixed(std::int32_t raw) : raw_(raw) {}

    std::int32_t raw_{0};
};

inline Fixed fixed_sim_delta()
{
    return Fixed::from_int(1) / Fixed::from_int(constants::SIM_TICKS_PER_SECOND);
}

inline Fixed fixed_lerp(const Fixed from, const Fixed to, const Fixed t)
{
    return from + (to - from) * t;
}

inline Fixed fixed_abs(const Fixed value)
{
    return value.raw() < 0 ? Fixed::from_raw(-value.raw()) : value;
}

inline Fixed tile_center_coord(const int cell)
{
    return Fixed::from_int(cell) + Fixed::from_int(1) / Fixed::from_int(2);
}

inline Fixed fixed_diagonal_step_length()
{
    return Fixed::from_float(1.41421356F);
}

inline int compute_move_segment_ticks(
    const Fixed from_x,
    const Fixed from_y,
    const Fixed to_x,
    const Fixed to_y,
    const int move_ticks_per_tile)
{
    const Fixed delta_x = fixed_abs(to_x - from_x);
    const Fixed delta_y = fixed_abs(to_y - from_y);

    Fixed distance = Fixed::from_int(1);
    if (delta_x.raw() > 0 && delta_y.raw() > 0) {
        distance = fixed_diagonal_step_length();
    }

    const Fixed total_ticks = distance * Fixed::from_int(move_ticks_per_tile);
    const int ticks = total_ticks.to_int();
    return ticks < 1 ? 1 : ticks;
}

} // namespace aoa::math
