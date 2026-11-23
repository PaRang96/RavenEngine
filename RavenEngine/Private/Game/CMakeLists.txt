cmake_minimum_required(VERSION 3.24)
project(RavenGameModule LANGUAGES CXX)
if(NOT WIN32 OR NOT RAVEN_ROOT OR NOT RAVEN_CORE_LIBRARY OR NOT RAVEN_GAME_OUTPUT)
    message(FATAL_ERROR "Configure this Windows development module through RavenEngine")
endif()
include("${RAVEN_ROOT}/cmake/RavenBuildSettings.cmake")
include("${RAVEN_ROOT}/cmake/RavenGameSources.cmake")
include("${RAVEN_ROOT}/cmake/GameContract.cmake")
raven_game_contract("${RAVEN_ROOT}" contract)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/GameContract.hpp" CONTENT
    "#pragma once\nnamespace Raven::GameBuild { inline constexpr char Contract[] = \"${contract}\"; }\n")
add_library(RavenGame SHARED ${RAVEN_GAME_SOURCES} ${RAVEN_GAME_HEADERS})
target_include_directories(RavenGame PRIVATE "${RAVEN_ROOT}/RavenEngine/Public" "${CMAKE_CURRENT_BINARY_DIR}")
# A file imported from the completed engine build: rebuilding this project never relinks RavenCore.
target_link_libraries(RavenGame PRIVATE "${RAVEN_CORE_LIBRARY}")
target_compile_definitions(RavenGame PRIVATE RAVEN_GAME_EXPORTS)
# A content-derived command key also detects edits with equal/coarse timestamps.
foreach(source IN LISTS RAVEN_GAME_SOURCES)
    file(SHA256 "${source}" source_digest)
    string(SHA256 build_digest "${contract}|${source}|${source_digest}")
    set_property(SOURCE "${source}" APPEND PROPERTY
        COMPILE_DEFINITIONS "RAVEN_GAME_BUILD_ID=${build_digest}")
endforeach()
target_compile_options(RavenGame PRIVATE /W4 /WX /utf-8)
# <format> dominates a one-file game edit; this PCH cut its compile from 1.75 to 1.0 s
# (docs/HotReloadDesign.md, H5). Engine headers stay out: adding them measured no gain.
raven_enable_precompiled_headers(RavenGame <cmath> <exception> <format> <numbers>)
set_target_properties(RavenGame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${RAVEN_GAME_OUTPUT}")

# Linker PDB files are not removed by Ninja's ordinary clean rule.
set(raven_game_pdb "$<TARGET_PDB_FILE:RavenGame>")
set_property(TARGET RavenGame APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${raven_game_pdb}")

# Expected runtime artifacts are derived from the target, not registered by hand.
# The supported Debug/RelWithDebInfo profiles produce a linker PDB.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/GameOutputs.txt" CONTENT
    "$<TARGET_FILE:RavenGame>\n$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:${raven_game_pdb}\n>")
