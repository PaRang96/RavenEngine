# Compilation and content-based build checks consume the same discovered files.
include("${CMAKE_CURRENT_LIST_DIR}/RavenSourceDiscovery.cmake")
get_filename_component(raven_game_directory
    "${CMAKE_CURRENT_LIST_DIR}/../RavenEngine/Private/Game" ABSOLUTE)
raven_collect_files(raven_game_files "${raven_game_directory}")
set(RAVEN_GAME_SOURCES "${raven_game_files}")
list(FILTER RAVEN_GAME_SOURCES INCLUDE REGEX "\\.cpp$")
set(RAVEN_GAME_HEADERS "${raven_game_files}")
list(FILTER RAVEN_GAME_HEADERS INCLUDE REGEX "\\.hpp$")
