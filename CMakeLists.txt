cmake_minimum_required(VERSION 3.24)

project(age-of-affinities
    VERSION 0.1.0
    DESCRIPTION "Age of Affinities RTS"
    LANGUAGES CXX
)

set(AOA_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD ${AOA_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Age of Affinities supports x64 Windows builds only.")
endif()

find_package(SFML CONFIG REQUIRED COMPONENTS System Window)
find_package(glad CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(aoa
    src/main.cpp
    src/app/application.cpp
    src/app/game_input.cpp
    src/app/window_display.cpp
    src/core/runtime_paths.cpp
    src/core/fixed_timestep_loop.cpp
    src/data/content_loader.cpp
    src/harness/regression_harness.cpp
    src/net/enet_transport.cpp
    src/net/lockstep_debug_log.cpp
    src/net/lockstep_runner.cpp
    src/net/lockstep_session.cpp
    src/net/lockstep_wire.cpp
    src/net/net_message.cpp
    src/net/net_smoke.cpp
    src/net/reconnect_wire.cpp
    src/render/camera.cpp
    src/render/camera_settings.cpp
    src/render/game_renderer.cpp
    src/render/hud_overlay.cpp
    src/render/sim_render_snapshot.cpp
    src/sim/map/test_map.cpp
    src/sim/player/player_commands.cpp
    src/sim/player/player_command.cpp
    src/sim/player/command_queue.cpp
    src/sim/scenario/test_scenario.cpp
    src/sim/spawn/unit_spawn.cpp
    src/sim/simulation.cpp
    src/sim/snapshot/entity_snapshot_key.cpp
    src/sim/snapshot/sim_snapshot.cpp
    src/sim/systems/disconnected_player_ai.cpp
    src/sim/systems/gameplay_systems.cpp
    src/sim/systems/pathfinding.cpp
    src/sim/systems/sim_systems.cpp
    src/sim/systems/visibility_system.cpp
)

target_include_directories(aoa PRIVATE src)
target_compile_definitions(aoa PRIVATE AOA_RUNTIME_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(aoa PRIVATE
    SFML::System
    SFML::Window
    glad::glad
    EnTT::EnTT
    unofficial::enet::enet
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(WIN32)
    target_link_libraries(aoa PRIVATE opengl32 ws2_32 winmm)
endif()

add_custom_command(
    TARGET aoa
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:aoa>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:aoa>/assets"
)

if(MSVC)
    target_compile_options(aoa PRIVATE /W4 /permissive-)
else()
    target_compile_options(aoa PRIVATE -Wall -Wextra -Wpedantic)
endif()
