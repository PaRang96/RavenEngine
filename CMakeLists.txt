cmake_minimum_required(VERSION 3.24)

# Enable Edit and Continue debug information for MSVC when supported.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<IF:$<CXX_COMPILER_ID:MSVC>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>"
    )
endif()

project(RavenEngine VERSION 0.1.0 LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RavenBuildSettings.cmake")

# Useful for clang-based tooling when using a compatible generator such as Ninja.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

add_subdirectory(RavenEngine)

# Tracked CTest suites. GPU-dependent tests carry the "gpu" label so CI can
# run CPU tests on machines without a Vulkan device (ctest -LE gpu).
# Source snapshots used by benchmark and smoke scripts may omit Tests/.
option(RAVEN_BUILD_TESTS "Build the RavenEngine CTest suites" ON)
if(RAVEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(Tests)
endif()
