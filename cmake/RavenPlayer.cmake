# Publish evaluated target paths for the player build command. Content selection
# is independent of the native target and uses the project and scene codecs.
if(NOT raven_player_platform OR NOT RAVEN_GAME_ENABLED)
    set(player_inputs "unavailable\tThis preset has no verified player backend with a game module.\n")
elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    set(player_inputs "unavailable\tChoose a Release preset for a player folder.\n")
else()
    add_custom_target(RavenPlayerInputs)
    add_dependencies(RavenPlayerInputs RavenEngine RavenCore RavenGame RavenSceneTool)
    string(CONCAT player_inputs
        "platform\t${raven_player_platform}\n"
        "tool\t$<TARGET_FILE:RavenSceneTool>\n"
        "copy\t$<TARGET_FILE:RavenEngine>\t$<TARGET_FILE_NAME:RavenEngine>\n"
        "copy\t$<TARGET_FILE:RavenCore>\t$<TARGET_FILE_NAME:RavenCore>\n"
        "copy\t${RAVEN_GAME_OUTPUT_DIRECTORY}/${RAVEN_GAME_MODULE_FILENAME}\t${RAVEN_GAME_MODULE_FILENAME}\n"
        "copy\t${raven_player_readme}\tREADME.txt\n")
    list(LENGTH RAVEN_PLAYER_SUPPORT_SOURCES player_file_count)
    math(EXPR last_player_file "${player_file_count} - 1")
    foreach(index RANGE ${last_player_file})
        list(GET RAVEN_PLAYER_SUPPORT_SOURCES ${index} source)
        list(GET RAVEN_PLAYER_SUPPORT_DESTINATIONS ${index} destination)
        string(APPEND player_inputs "copy\t${source}\t${destination}\n")
    endforeach()
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PlayerInputs.txt" CONTENT "${player_inputs}")
