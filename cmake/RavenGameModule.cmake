# The standalone child links a completed import library, without a build edge
# back to a loaded RavenCore.dll or host executable.
set(RAVEN_GAME_DEFAULT OFF)
if(WIN32 AND MSVC AND CMAKE_GENERATOR STREQUAL "Ninja")
    set(RAVEN_GAME_DEFAULT ON)
endif()
option(RAVEN_BUILD_GAME_MODULE "Build the Windows development game module" ${RAVEN_GAME_DEFAULT})
set(RAVEN_GAME_ENABLED false)
set(RAVEN_GAME_CONTRACT "unsupported")
set(RAVEN_GAME_MODULE_FILENAME "${CMAKE_SHARED_LIBRARY_PREFIX}RavenGame${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(RAVEN_GAME_BUILD_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/GameBuild")
if(RAVEN_BUILD_GAME_MODULE)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_GENERATOR STREQUAL "Ninja")
        message(FATAL_ERROR "Game module builds currently require Windows, MSVC and single-config Ninja")
    endif()
    set(RAVEN_GAME_ENABLED true)
    include("${PROJECT_SOURCE_DIR}/cmake/GameContract.cmake")
    raven_game_contract("${PROJECT_SOURCE_DIR}" RAVEN_GAME_CONTRACT)

    include("${PROJECT_SOURCE_DIR}/cmake/RavenBuildEnvironment.cmake")
    set(RAVEN_GAME_ENVIRONMENT_FILE "${RAVEN_GAME_BUILD_DIRECTORY}/BuildEnvironment.cmake")
    raven_capture_msvc_environment("${RAVEN_GAME_ENVIRONMENT_FILE}")

    # Use the same content-checked recipe for full builds and runtime game builds.
    set(RAVEN_GAME_CORE_LIBRARY "$<TARGET_LINKER_FILE:RavenCore>")
    set(RAVEN_GAME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:RavenCore>")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/BuildGame.cmake.in"
        "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake.in" @ONLY)
    file(GENERATE OUTPUT "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake"
        INPUT "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake.in")
    add_custom_target(RavenGame ALL
        COMMAND "${CMAKE_COMMAND}" -P "${RAVEN_GAME_BUILD_DIRECTORY}/BuildGame.cmake"
        DEPENDS RavenCore
        COMMENT "Check and build standalone reloadable game module"
        VERBATIM
    )
endif()
configure_file("${PROJECT_SOURCE_DIR}/cmake/GameBuildConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/GameBuildConfig.hpp" @ONLY)
