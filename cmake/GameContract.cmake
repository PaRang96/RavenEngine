# Conservative compatibility: shared declarations, game definitions and build rules.
# Function bodies in game .cpp files are deliberately excluded.
function(raven_game_contract root output)
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS
        "${root}/RavenEngine/Public/*.hpp"
        "${root}/RavenEngine/Private/Game/*.hpp"
        "${root}/cmake/*.cmake" "${root}/cmake/*.in")
    list(APPEND headers "${root}/CMakeLists.txt" "${root}/RavenEngine/CMakeLists.txt"
        "${root}/CMakePresets.json" "${root}/RavenEngine/Private/Game/CMakeLists.txt"
        "${root}/cmake/GameContract.cmake" "${root}/cmake/GameBuildConfig.hpp.in")
    list(SORT headers)
    list(REMOVE_DUPLICATES headers)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${headers})
    string(TOUPPER "${CMAKE_BUILD_TYPE}" configuration)
    set(data "${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_SIZEOF_VOID_P}|${CMAKE_BUILD_TYPE}|${CMAKE_CXX_FLAGS}|${CMAKE_CXX_FLAGS_${configuration}}|C++20|MD")
    foreach(header IN LISTS headers)
        file(RELATIVE_PATH name "${root}" "${header}")
        file(SHA256 "${header}" digest)
        string(APPEND data "\n${name}:${digest}")
    endforeach()
    string(SHA256 contract "${data}")
    set(${output} "${contract}" PARENT_SCOPE)
endfunction()
