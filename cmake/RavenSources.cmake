# Source ownership follows folders. The common scan is also used to select the
# sample and active native backend, rather than rescanning for every extension.
include("${PROJECT_SOURCE_DIR}/cmake/RavenSourceDiscovery.cmake")
raven_collect_files(raven_public_files "${CMAKE_CURRENT_SOURCE_DIR}/Public")
raven_collect_files(raven_private_files "${CMAKE_CURRENT_SOURCE_DIR}/Private")

set(raven_public_headers "${raven_public_files}")
list(FILTER raven_public_headers INCLUDE REGEX "\\.hpp$")
set(raven_engine_sources "${raven_private_files}")
list(FILTER raven_engine_sources INCLUDE REGEX "\\.(cpp|hpp)$")
list(FILTER raven_engine_sources EXCLUDE REGEX "/Private/(Game|Samples|Platform)/")

# Shared platform support is nonrecursive. The fallback module implementation is
# selected explicitly in RavenPlatform.cmake together with the native backend.
set(raven_platform_shared "${raven_private_files}")
list(FILTER raven_platform_shared INCLUDE REGEX "/Private/Platform/[^/]+\\.(cpp|hpp)$")
list(FILTER raven_platform_shared EXCLUDE REGEX "/UnsupportedGameModule\\.cpp$")
target_sources(RavenCore PRIVATE
    ${raven_public_headers} ${raven_engine_sources} ${raven_platform_shared})

set(raven_sample_sources "${raven_private_files}")
list(FILTER raven_sample_sources INCLUDE REGEX "/Private/Samples/.*\\.(cpp|hpp|h)$")
target_sources(RavenEngine PRIVATE ${raven_sample_sources})
