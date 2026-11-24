# Game roots (docs/DataModelDesign.md, FD-17): every Games/<Name> folder with a CMakeLists.txt is
# one game with its own module, content and child build. The standalone child links a completed
# import library, without a build edge back to a loaded RavenCore.dll or host executable.
set(RAVEN_GAME_DEFAULT OFF)
if(WIN32 AND MSVC AND CMAKE_GENERATOR STREQUAL "Ninja")
    set(RAVEN_GAME_DEFAULT ON)
endif()
option(RAVEN_BUILD_GAME_MODULE "Build the Windows development game modules" ${RAVEN_GAME_DEFAULT})
set(RAVEN_DEFAULT_GAME "Samples" CACHE STRING "The game root RavenEngine runs without --game")

set(RAVEN_GAMES)
file(GLOB raven_game_roots LIST_DIRECTORIES true CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Games/*")
foreach(raven_game_root IN LISTS raven_game_roots)
    if(EXISTS "${raven_game_root}/CMakeLists.txt")
        get_filename_component(raven_game_name "${raven_game_root}" NAME)
        list(APPEND RAVEN_GAMES "${raven_game_name}")
    endif()
endforeach()
if(NOT RAVEN_DEFAULT_GAME IN_LIST RAVEN_GAMES)
    message(FATAL_ERROR "RAVEN_DEFAULT_GAME '${RAVEN_DEFAULT_GAME}' is not a folder under Games/")
endif()

set(RAVEN_GAME_ENABLED false)
set(RAVEN_GAME_CONTRACT "unsupported")
set(RAVEN_GAME_BUILD_ROOT "${CMAKE_CURRENT_BINARY_DIR}/GameBuild")
set(RAVEN_GAME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:RavenCore>")
if(RAVEN_BUILD_GAME_MODULE)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_GENERATOR STREQUAL "Ninja")
        message(FATAL_ERROR "Game module builds currently require Windows, MSVC and single-config Ninja")
    endif()
    set(RAVEN_GAME_ENABLED true)
    include("${PROJECT_SOURCE_DIR}/cmake/GameContract.cmake")
    raven_game_contract("${PROJECT_SOURCE_DIR}" RAVEN_GAME_CONTRACT)

    include("${PROJECT_SOURCE_DIR}/cmake/RavenBuildEnvironment.cmake")
    set(RAVEN_GAME_ENVIRONMENT_FILE "${RAVEN_GAME_BUILD_ROOT}/BuildEnvironment.cmake")
    raven_capture_msvc_environment("${RAVEN_GAME_ENVIRONMENT_FILE}")

    # Use the same content-checked recipe for full builds and runtime game builds.
    set(RAVEN_GAME_CORE_LIBRARY "$<TARGET_LINKER_FILE:RavenCore>")
    foreach(RAVEN_GAME_NAME IN LISTS RAVEN_GAMES)
        set(RAVEN_GAME_SOURCE_DIR "${PROJECT_SOURCE_DIR}/Games/${RAVEN_GAME_NAME}")
        set(RAVEN_GAME_BUILD_DIRECTORY "${RAVEN_GAME_BUILD_ROOT}/${RAVEN_GAME_NAME}")
        configure_file("${PROJECT_SOURCE_DIR}/cmake/BuildGame.cmake.in"
            "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake.in" @ONLY)
        file(GENERATE OUTPUT "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake"
            INPUT "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake.in")
        # One generated list of the folders the running host watches for this game: its root, and
        # the folders of the engine contract inputs, whose edits are reported as needing a restart.
        file(WRITE "${RAVEN_GAME_BUILD_DIRECTORY}/WatchedInputs.txt"
            "${RAVEN_GAME_SOURCE_DIR}\n${PROJECT_SOURCE_DIR}/RavenEngine/Public\n${PROJECT_SOURCE_DIR}/cmake\n")
        add_custom_target(${RAVEN_GAME_NAME}Game ALL
            COMMAND "${CMAKE_COMMAND}" -P "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake"
            DEPENDS RavenCore
            COMMENT "Check and build the reloadable ${RAVEN_GAME_NAME} module"
            VERBATIM
        )
    endforeach()
endif()
configure_file("${PROJECT_SOURCE_DIR}/cmake/GameBuildConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/GameBuildConfig.hpp" @ONLY)

# A game root may also carry tools (Games/<Name>/Tools/*.cpp), for example scene generators. They
# build with the host, against the game's own headers, and never load into a running host.
foreach(raven_game_name IN LISTS RAVEN_GAMES)
    file(GLOB raven_game_tool_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Games/${raven_game_name}/Tools/*.cpp")
    if(raven_game_tool_sources)
        add_executable(${raven_game_name}Tool ${raven_game_tool_sources})
        target_include_directories(${raven_game_name}Tool PRIVATE
            "${PROJECT_SOURCE_DIR}/Games/${raven_game_name}/Source"
            "${PROJECT_SOURCE_DIR}/Games/${raven_game_name}/Tools")
        target_link_libraries(${raven_game_name}Tool PRIVATE RavenCore)
        raven_apply_warnings(${raven_game_name}Tool)
    endif()
endforeach()
