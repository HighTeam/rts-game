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
    src/render/gl_renderer.cpp
    src/sim/simulation.cpp
    src/sim/systems/sim_systems.cpp
)

target_include_directories(aoa PRIVATE src)

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

if(MSVC)
    target_compile_options(aoa PRIVATE /W4 /permissive-)
else()
    target_compile_options(aoa PRIVATE -Wall -Wextra -Wpedantic)
endif()
