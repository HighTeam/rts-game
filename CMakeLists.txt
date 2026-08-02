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

add_executable(aoa
    src/main.cpp
    src/app/application.cpp
    src/core/fixed_timestep_loop.cpp
    src/data/content_loader.cpp
    src/harness/regression_harness.cpp
    src/render/camera.cpp
    src/render/camera_settings.cpp
    src/render/game_renderer.cpp
    src/render/hud_overlay.cpp
    src/sim/map/test_map.cpp
    src/sim/scenario/test_scenario.cpp
    src/sim/simulation.cpp
    src/sim/systems/gameplay_systems.cpp
    src/sim/systems/pathfinding.cpp
    src/sim/systems/sim_systems.cpp
)

target_include_directories(aoa PRIVATE src)
target_compile_definitions(aoa PRIVATE AOA_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

target_link_libraries(aoa PRIVATE
    SFML::System
    SFML::Window
    glad::glad
    EnTT::EnTT
    unofficial::enet::enet
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(aoa PRIVATE opengl32)
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
