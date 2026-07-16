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

find_package(SFML CONFIG REQUIRED COMPONENTS System)
find_package(glad CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(unofficial-enet CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(aoa src/main.cpp)

target_link_libraries(aoa PRIVATE
    SFML::System
    glad::glad
    EnTT::EnTT
    unofficial::enet::enet
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(aoa PRIVATE /W4 /permissive-)
else()
    target_compile_options(aoa PRIVATE -Wall -Wextra -Wpedantic)
endif()
