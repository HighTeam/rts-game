cmake_minimum_required(VERSION 3.24)

project(age-of-affinities
    VERSION 0.2.1
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

find_package(SFML CONFIG REQUIRED COMPONENTS System Window Graphics Audio)
find_package(glad CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(aoa_pack_assets
    tools/pack_assets.cpp
)

add_executable(aoa
    src/main.cpp
    src/app/app_settings.cpp
    src/app/app_shell.cpp
    src/app/application.cpp
    src/app/game_cursor.cpp
    src/app/game_input.cpp
    src/app/main_menu.cpp
    src/app/native_file_dialog.cpp
    src/app/window_display.cpp
    src/audio/game_audio.cpp
    src/core/asset_io.cpp
    src/core/asset_store.cpp
    src/core/runtime_paths.cpp
    src/core/fixed_timestep_loop.cpp
    src/data/content_loader.cpp
    src/harness/regression_harness.cpp
    src/net/enet_transport.cpp
    src/net/lan_discovery.cpp
    src/net/lobby_session.cpp
    src/net/lobby_wire.cpp
    src/net/lockstep_auto_input.cpp
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
    src/render/hud_icons.cpp
    src/render/hud_overlay.cpp
    src/render/menu_background.cpp
    src/render/menu_renderer.cpp
    src/render/scene_textures.cpp
    src/render/sim_render_snapshot.cpp
    src/sim/map/map_generator.cpp
    src/sim/map/map_pattern.cpp
    src/sim/map/test_map.cpp
    src/sim/player/player_commands.cpp
    src/sim/player/player_command.cpp
    src/sim/player/player_economy.cpp
    src/sim/player/command_queue.cpp
    src/sim/scenario/scenario_file.cpp
    src/sim/scenario/scenario_layouts.cpp
    src/sim/scenario/test_scenario.cpp
    src/sim/spawn/unit_spawn.cpp
    src/sim/spawn/building_unit_spawn.cpp
    src/sim/persistence/save_game.cpp
    src/sim/simulation.cpp
    src/sim/snapshot/entity_snapshot_key.cpp
    src/sim/snapshot/sim_snapshot.cpp
    src/sim/systems/disconnected_player_ai.cpp
    src/sim/systems/gameplay_systems.cpp
    src/sim/systems/match_outcome.cpp
    src/sim/systems/pathfinding.cpp
    src/sim/systems/sim_systems.cpp
    src/sim/systems/visibility_system.cpp
)

target_include_directories(aoa PRIVATE src)
target_compile_definitions(aoa PRIVATE AOA_RUNTIME_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(aoa PRIVATE
    SFML::System
    SFML::Window
    SFML::Graphics
    SFML::Audio
    glad::glad
    EnTT::EnTT
    unofficial::enet::enet
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(WIN32)
    target_link_libraries(aoa PRIVATE opengl32 ws2_32 winmm comdlg32 iphlpapi)
    set_target_properties(aoa PROPERTIES
        OUTPUT_NAME AgeofAffinities
        WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(aoa PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

add_dependencies(aoa aoa_pack_assets)

add_custom_command(
    TARGET aoa
    POST_BUILD
    COMMAND $<TARGET_FILE:aoa_pack_assets>
        "${CMAKE_SOURCE_DIR}/assets"
        "${CMAKE_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:aoa>/assets.dat"
    # Drop leftover loose trees so a zipped Release only needs exe + DLLs + assets.dat.
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_FILE_DIR:aoa>/assets"
        "$<TARGET_FILE_DIR:aoa>/data"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:aoa>/scenarios"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scenarios"
        "$<TARGET_FILE_DIR:aoa>/scenarios"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:aoa>/patterns"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/patterns"
        "$<TARGET_FILE_DIR:aoa>/patterns"
)

add_executable(aoa_pattern_maker
    src/pattern_maker/main.cpp
    src/app/native_file_dialog.cpp
    src/core/runtime_paths.cpp
    src/sim/map/map_generator.cpp
    src/sim/map/map_pattern.cpp
    src/sim/map/test_map.cpp
    src/sim/scenario/scenario_layouts.cpp
)

set_target_properties(aoa_pattern_maker PROPERTIES OUTPUT_NAME PatternMaker)
target_include_directories(aoa_pattern_maker PRIVATE src)
target_compile_definitions(aoa_pattern_maker PRIVATE AOA_RUNTIME_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(aoa_pattern_maker PRIVATE
    SFML::System
    SFML::Window
    SFML::Graphics
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(aoa_pattern_maker PRIVATE comdlg32)
endif()

add_custom_command(
    TARGET aoa_pattern_maker
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:aoa_pattern_maker>/patterns"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/patterns"
        "$<TARGET_FILE_DIR:aoa_pattern_maker>/patterns"
)

if(MSVC)
    target_compile_options(aoa_pack_assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(aoa_pack_assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(aoa PRIVATE /W4 /permissive-)
    target_compile_options(aoa_pattern_maker PRIVATE /W4 /permissive-)
else()
    target_compile_options(aoa PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(aoa_pattern_maker PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_custom_target(aoa_setup
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/package-setup.ps1"
            -SourceDir "$<TARGET_FILE_DIR:aoa>"
            -RepoRoot "${CMAKE_SOURCE_DIR}"
        DEPENDS aoa aoa_pattern_maker
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Building unsigned aoa-setup.exe with NSIS"
        VERBATIM
    )
endif()
