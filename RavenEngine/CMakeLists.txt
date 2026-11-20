add_library(RavenCore SHARED)
add_executable(RavenEngine)
add_executable(RavenSceneTool)

include("${PROJECT_SOURCE_DIR}/cmake/RavenSources.cmake")

target_compile_definitions(RavenCore PRIVATE
    RAVEN_CORE_EXPORTS
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:RAVEN_DEVELOPMENT>
    $<$<CONFIG:Debug>:RAVEN_ENABLE_VALIDATION>
)
target_include_directories(RavenCore PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
    ${Vulkan_INCLUDE_DIRS}
)
target_include_directories(RavenCore PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/$<CONFIG>"
)
target_include_directories(RavenCore SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/ThirdParty/stb")

target_compile_features(RavenCore PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
add_library(RavenFbx STATIC "${PROJECT_SOURCE_DIR}/ThirdParty/ufbx/ufbx.c")
set_target_properties(RavenFbx PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(RavenFbx SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/ThirdParty/ufbx")
target_link_libraries(RavenCore PRIVATE Vulkan::Vulkan RavenFbx)

target_include_directories(RavenEngine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Samples"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated"
)
target_link_libraries(RavenEngine PRIVATE RavenCore)
target_link_libraries(RavenSceneTool PRIVATE RavenCore)
target_include_directories(RavenSceneTool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")

# Each module handles one build responsibility for the targets above.
include("${PROJECT_SOURCE_DIR}/cmake/RavenContent.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenPlatform.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenGameModule.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenPlayer.cmake")

if(RAVEN_GAME_ENABLED)
    add_executable(Raven3DSmoke EXCLUDE_FROM_ALL
        "${PROJECT_SOURCE_DIR}/scripts/Windows3DSmoke.cpp"
        "Private/Samples/PerspectiveLevel.cpp")
    target_include_directories(Raven3DSmoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Private"
        "${CMAKE_CURRENT_SOURCE_DIR}/Private/Samples")
    target_link_libraries(Raven3DSmoke PRIVATE RavenCore)
    add_dependencies(Raven3DSmoke RavenGame)
endif()

raven_enable_precompiled_headers(RavenCore)
raven_enable_precompiled_headers(RavenEngine)
