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

target_compile_features(RavenCore PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
# Only RAVEN_API symbols leave RavenCore (AA2-PLAT-04); Windows already behaves this way.
set_target_properties(RavenCore PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
include("${PROJECT_SOURCE_DIR}/cmake/RavenUfbx.cmake")
raven_add_ufbx()
include("${PROJECT_SOURCE_DIR}/cmake/RavenStb.cmake")
raven_add_stb()
include("${PROJECT_SOURCE_DIR}/cmake/RavenVma.cmake")
raven_add_vma()
include("${PROJECT_SOURCE_DIR}/cmake/RavenJolt.cmake")
raven_add_jolt()
target_link_libraries(RavenCore PRIVATE Vulkan::Vulkan RavenFbx RavenStb RavenVma Jolt)
include("${PROJECT_SOURCE_DIR}/cmake/RavenAudio.cmake")
raven_add_audio(RavenCore)
include("${PROJECT_SOURCE_DIR}/cmake/RavenVorbis.cmake")
raven_add_vorbis(RavenCore)
include("${PROJECT_SOURCE_DIR}/cmake/RavenText.cmake")
raven_add_text(RavenCore)

target_include_directories(RavenEngine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Generated")
target_link_libraries(RavenEngine PRIVATE RavenCore)
target_link_libraries(RavenSceneTool PRIVATE RavenCore)
target_include_directories(RavenSceneTool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")

# Each module handles one build responsibility for the targets above.
include("${PROJECT_SOURCE_DIR}/cmake/RavenContent.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenPlatform.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenGameModule.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenPlayer.cmake")

if(RAVEN_GAME_ENABLED)
    # The engine session is internal to RavenCore, so the smoke compiles its own copy.
    add_executable(Raven3DSmoke EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/scripts/Windows3DSmoke.cpp"
        "Private/Core/GameSession.cpp")
    target_include_directories(Raven3DSmoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Private"
        "${CMAKE_CURRENT_BINARY_DIR}/Generated"
        "${PROJECT_SOURCE_DIR}/Games/Samples/Source")
    target_link_libraries(Raven3DSmoke PRIVATE RavenCore)
    add_dependencies(Raven3DSmoke SamplesGame)
endif()

raven_enable_precompiled_headers(RavenCore)
raven_enable_precompiled_headers(RavenEngine)
foreach(raven_target IN ITEMS RavenCore RavenEngine RavenSceneTool)
    raven_apply_warnings(${raven_target})
endforeach()
if(TARGET Raven3DSmoke)
    raven_apply_warnings(Raven3DSmoke)
endif()
