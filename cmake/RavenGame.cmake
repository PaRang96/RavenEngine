# Builds one game root's module against the completed engine (docs/DataModelDesign.md, FD-17).
# Each Games/<Name>/CMakeLists.txt calls raven_add_game(<Name>) from a standalone child project
# that RavenEngine configures; the game sees only RavenEngine/Public.
if(NOT WIN32 OR NOT RAVEN_ROOT OR NOT RAVEN_CORE_LIBRARY OR NOT RAVEN_GAME_OUTPUT)
    message(FATAL_ERROR "Configure game modules through RavenEngine")
endif()
include("${RAVEN_ROOT}/cmake/RavenBuildSettings.cmake")
include("${RAVEN_ROOT}/cmake/RavenGameSources.cmake")
include("${RAVEN_ROOT}/cmake/GameContract.cmake")

function(raven_add_game name)
    raven_game_contract("${RAVEN_ROOT}" contract)
    raven_game_sources("${CMAKE_CURRENT_SOURCE_DIR}" sources headers)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/GameContract.hpp" CONTENT
        "#pragma once\nnamespace Raven::GameBuild { inline constexpr char Contract[] = \"${contract}\"; }\n")
    add_library(${name} SHARED ${sources} ${headers})
    target_include_directories(${name} PRIVATE "${RAVEN_ROOT}/RavenEngine/Public" "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    # A file imported from the completed engine build: rebuilding this project never relinks RavenCore.
    target_link_libraries(${name} PRIVATE "${RAVEN_CORE_LIBRARY}")
    target_compile_definitions(${name} PRIVATE RAVEN_GAME_EXPORTS)
    # A content-derived command key also detects edits with equal/coarse timestamps.
    foreach(source IN LISTS sources)
        file(SHA256 "${source}" source_digest)
        string(SHA256 build_digest "${contract}|${source}|${source_digest}")
        set_property(SOURCE "${source}" APPEND PROPERTY
            COMPILE_DEFINITIONS "RAVEN_GAME_BUILD_ID=${build_digest}")
    endforeach()
    target_compile_options(${name} PRIVATE /utf-8)
    raven_apply_warnings(${name})
    # Debug info, so breakpoints bind in game code. /Zi rather than /ZI: the module is replaced by
    # reloading, not by Edit and Continue.
    if(POLICY CMP0141)
        set_property(TARGET ${name} PROPERTY MSVC_DEBUG_INFORMATION_FORMAT
            "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
    else()
        # CMake 3.24 takes the format from the flags, which the host may pass without /Zi.
        target_compile_options(${name} PRIVATE $<$<CONFIG:Debug,RelWithDebInfo>:/Zi> $<$<CONFIG:Debug,RelWithDebInfo>:/FS>)
    endif()
    # <format> dominates a one-file game edit; this PCH cut its compile from 1.75 to 1.0 s
    # (docs/HotReloadDesign.md, H5). Since M6C the game-facing engine headers (systems, input
    # mapping, UI, sound, physics) cost a one-file edit more than <format> did, so they are
    # precompiled too; they change only with a host rebuild (docs/GameServicesDesign.md, C10).
    file(GLOB game_component_headers RELATIVE "${RAVEN_ROOT}/RavenEngine/Public"
        "${RAVEN_ROOT}/RavenEngine/Public/Raven/Components/*.hpp")
    list(TRANSFORM game_component_headers PREPEND "<")
    list(TRANSFORM game_component_headers APPEND ">")
    raven_enable_precompiled_headers(${name} <cmath> <exception> <format> <numbers>
        <Raven/Core/GameSystems.hpp> <Raven/Core/Log.hpp> <Raven/Scene/SceneCommands.hpp>
        ${game_component_headers})
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${RAVEN_GAME_OUTPUT}")

    # Linker PDB files are not removed by Ninja's ordinary clean rule.
    set(pdb "$<TARGET_PDB_FILE:${name}>")
    set_property(TARGET ${name} APPEND PROPERTY ADDITIONAL_CLEAN_FILES "${pdb}")

    # Expected runtime artifacts are derived from the target, not registered by hand.
    # The supported Debug/RelWithDebInfo profiles produce a linker PDB.
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/GameOutputs.txt" CONTENT
        "$<TARGET_FILE:${name}>\n$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:${pdb}\n>")
endfunction()
