cmake_minimum_required(VERSION 3.24)
project(RavenGameModule LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT WIN32 OR NOT RAVEN_ROOT OR NOT RAVEN_CORE_LIBRARY OR NOT RAVEN_GAME_OUTPUT)
    message(FATAL_ERROR "Configure this Windows development module through RavenEngine")
endif()
include("${RAVEN_ROOT}/cmake/GameContract.cmake")
raven_game_contract("${RAVEN_ROOT}" contract)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/GameContract.hpp" CONTENT
    "#pragma once\nnamespace Raven::GameBuild { inline constexpr char Contract[] = \"${contract}\"; }\n")
add_library(RavenGame SHARED RavenGame.cpp GameComponents.hpp)
target_include_directories(RavenGame PRIVATE "${RAVEN_ROOT}/RavenEngine/Public" "${CMAKE_CURRENT_BINARY_DIR}")
# A file imported from the completed engine build: rebuilding this project never relinks RavenCore.
target_link_libraries(RavenGame PRIVATE "${RAVEN_CORE_LIBRARY}")
target_compile_definitions(RavenGame PRIVATE RAVEN_GAME_EXPORTS)
# A content-derived command key also detects edits with equal/coarse timestamps.
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/RavenGame.cpp" source_digest)
string(SHA256 build_digest "${contract}|${source_digest}")
target_compile_definitions(RavenGame PRIVATE "RAVEN_GAME_BUILD_ID=${build_digest}")
target_compile_options(RavenGame PRIVATE /W4 /WX /utf-8)
set_target_properties(RavenGame PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    RUNTIME_OUTPUT_DIRECTORY "${RAVEN_GAME_OUTPUT}")
