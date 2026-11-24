include("${CMAKE_CURRENT_LIST_DIR}/RavenSourceDiscovery.cmake")

# The engine's public surface (docs/DataModelDesign.md, FD-17, contract (ii)): the public headers,
# the CMake that compiles game modules, and the toolchain. Game code is not part of it: a game's
# components are checked field by field when its module activates, so editing them reloads.
function(raven_game_contract_inputs root output)
    raven_collect_files(inputs "${root}/RavenEngine/Public")
    list(FILTER inputs INCLUDE REGEX "\\.hpp$")
    list(APPEND inputs
        "${root}/cmake/GameContract.cmake"
        "${root}/cmake/RavenBuildSettings.cmake"
        "${root}/cmake/RavenGame.cmake"
        "${root}/cmake/RavenGameSources.cmake")
    list(SORT inputs)
    list(REMOVE_DUPLICATES inputs)
    set(${output} "${inputs}" PARENT_SCOPE)
endfunction()

function(raven_game_contract root output)
    raven_game_contract_inputs("${root}" headers)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${headers})
    string(TOUPPER "${CMAKE_BUILD_TYPE}" configuration)
    string(CONCAT data
        "${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_SIZEOF_VOID_P}|"
        "${CMAKE_BUILD_TYPE}|${CMAKE_CXX_FLAGS}|${CMAKE_CXX_FLAGS_${configuration}}|"
        "C++${CMAKE_CXX_STANDARD}|${CMAKE_CXX${CMAKE_CXX_STANDARD}_STANDARD_COMPILE_OPTION}|"
        "${CMAKE_MSVC_RUNTIME_LIBRARY}")
    foreach(header IN LISTS headers)
        file(RELATIVE_PATH name "${root}" "${header}")
        file(SHA256 "${header}" digest)
        string(APPEND data "\n${name}:${digest}")
    endforeach()
    string(SHA256 contract "${data}")
    set(${output} "${contract}" PARENT_SCOPE)
endfunction()
