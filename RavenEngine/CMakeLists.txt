add_library(RavenCore SHARED)
add_executable(RavenEngine)

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
)
target_include_directories(RavenCore SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/ThirdParty/stb")

target_compile_features(RavenCore PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
target_link_libraries(RavenCore PRIVATE Vulkan::Vulkan)

target_include_directories(RavenEngine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/Samples"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(RavenEngine PRIVATE RavenCore)

# Each module handles one build responsibility for the targets above.
include("${PROJECT_SOURCE_DIR}/cmake/RavenContent.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenPlatform.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/RavenGameModule.cmake")

raven_enable_precompiled_headers(RavenCore)
raven_enable_precompiled_headers(RavenEngine)
