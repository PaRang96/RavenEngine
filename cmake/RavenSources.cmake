# Source ownership follows folders. The common scan is also used to select the
# sample and active native backend, rather than rescanning for every extension.
include("${PROJECT_SOURCE_DIR}/cmake/RavenSourceDiscovery.cmake")
raven_collect_files(raven_public_files "${CMAKE_CURRENT_SOURCE_DIR}/Public")
raven_collect_files(raven_private_files "${CMAKE_CURRENT_SOURCE_DIR}/Private")

set(raven_public_headers "${raven_public_files}")
list(FILTER raven_public_headers INCLUDE REGEX "\\.hpp$")
set(raven_engine_sources "${raven_private_files}")
list(FILTER raven_engine_sources INCLUDE REGEX "\\.(cpp|hpp)$")
# Audio device backends are selected by RAVEN_AUDIO_BACKEND in RavenAudio.cmake.
list(FILTER raven_engine_sources EXCLUDE REGEX "/Private/(Host|Tools|Platform|Audio/Backends)/")

# Shared platform support is nonrecursive. RavenPlatform.cmake selects fallbacks
# together with the native backends.
set(raven_platform_shared "${raven_private_files}")
list(FILTER raven_platform_shared INCLUDE REGEX "/Private/Platform/[^/]+\\.(cpp|hpp)$")
list(FILTER raven_platform_shared EXCLUDE REGEX "/(Unsupported(GameModule|KeyNames)|PosixAtomicFile|PollDirectoryWatch)\\.cpp$")
target_sources(RavenCore PRIVATE
    ${raven_public_headers} ${raven_engine_sources} ${raven_platform_shared})

set(raven_host_sources "${raven_private_files}")
list(FILTER raven_host_sources INCLUDE REGEX "/Private/Host/.*\\.(cpp|hpp|h)$")
target_sources(RavenEngine PRIVATE ${raven_host_sources})

set(raven_tool_sources "${raven_private_files}")
list(FILTER raven_tool_sources INCLUDE REGEX "/Private/Tools/.*\\.(cpp|hpp)$")
target_sources(RavenSceneTool PRIVATE ${raven_tool_sources})
