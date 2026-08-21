#pragma once



#include "core/grid.hpp"



#include <entt/entt.hpp>



#include <optional>

#include <vector>



namespace aoa::app {



struct PlayerSelection {

    std::vector<entt::entity> units{};

    std::optional<core::GridPos> resource_cell{};

    entt::entity building{entt::null};



    void clear()

    {

        units.clear();

        resource_cell.reset();

        building = entt::null;

    }



    void clear_units()

    {

        units.clear();

    }



    void clear_resource()

    {

        resource_cell.reset();

    }



    void clear_building()

    {

        building = entt::null;

    }



    [[nodiscard]] bool has_units() const { return !units.empty(); }

    [[nodiscard]] bool has_resource() const { return resource_cell.has_value(); }

    [[nodiscard]] bool has_building() const { return building != entt::null; }

};



struct HoverHighlight {

    entt::entity unit{entt::null};

    bool unit_is_enemy{false};

    entt::entity building{entt::null};

    bool building_is_enemy{false};

    std::optional<core::GridPos> resource_cell{};

};



} // namespace aoa::app

