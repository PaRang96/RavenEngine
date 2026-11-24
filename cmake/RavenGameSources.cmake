# Compilation and content-based build checks consume the same discovered files: everything under
# a game root's Source folder.
include("${CMAKE_CURRENT_LIST_DIR}/RavenSourceDiscovery.cmake")

function(raven_game_sources game_root sources headers)
    raven_collect_files(files "${game_root}/Source")
    set(cpp "${files}")
    list(FILTER cpp INCLUDE REGEX "\\.cpp$")
    set(hpp "${files}")
    list(FILTER hpp INCLUDE REGEX "\\.hpp$")
    set(${sources} "${cpp}" PARENT_SCOPE)
    set(${headers} "${hpp}" PARENT_SCOPE)
endfunction()
