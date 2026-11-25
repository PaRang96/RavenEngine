# Jolt Physics, built as an engine-private static library linked into RavenCore
# (ThirdParty/JoltPhysics/README.md, docs/GameServicesDesign.md FD-32).
#
# ThirdParty/JoltPhysics is an unmodified copy of an upstream release, replaced only by
# scripts/UpdateJolt.cmake. Every Raven setting lives in this file, so updating Jolt means
# running that script and changing RAVEN_JOLT_VERSION below.
include_guard(GLOBAL)

set(RAVEN_JOLT_VERSION 5.7.0)
set(RAVEN_JOLT_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/JoltPhysics")
# Turn off to step through Jolt's own code in a debugger; physics then runs several times slower.
option(RAVEN_JOLT_OPTIMIZE_DEBUG "Compile Jolt with optimization in Debug builds" ON)

function(raven_add_jolt)
    file(STRINGS "${RAVEN_JOLT_ROOT}/Build/CMakeLists.txt" project_line REGEX "^project\\(JoltPhysics VERSION ")
    if(NOT project_line MATCHES "VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" OR
        NOT CMAKE_MATCH_1 VERSION_EQUAL RAVEN_JOLT_VERSION)
        message(FATAL_ERROR "ThirdParty/JoltPhysics does not hold Jolt ${RAVEN_JOLT_VERSION}. "
            "Update it with scripts/UpdateJolt.cmake and keep RAVEN_JOLT_VERSION in step (ThirdParty/JoltPhysics/README.md).")
    endif()

    # Jolt's own options, read by its build file. These variables are local to this function,
    # so they reach Jolt's directory only.
    set(JPH_BUILD_SHARED_LIBS OFF)
    set(ENABLE_INSTALL OFF)
    # Raven's dynamic runtime and its own optimization flags.
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF)
    set(OVERRIDE_CXX_FLAGS OFF)
    # Raven's warning level does not apply to third-party code.
    set(ENABLE_ALL_WARNINGS OFF)
    # RavenCore is built with RTTI; a mismatch fails to link.
    set(CPP_RTTI_ENABLED ON)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
    # Jolt's own serialization of settings; Raven stores physics in components and scene files.
    set(ENABLE_OBJECT_STREAM OFF)
    # The collider view (F10) draws through Jolt's debug renderer.
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON)
    # Link-time optimization is decided after measuring (FD-32).
    set(INTERPROCEDURAL_OPTIMIZATION OFF)
    # GPU compute serves only Jolt's hair simulation, which Raven does not use.
    set(JPH_USE_DX12 OFF)
    set(JPH_USE_VK OFF)
    set(JPH_USE_MTL OFF)
    set(JPH_USE_CPU_COMPUTE OFF)
    # Debug builds keep Jolt optimized: unoptimized Jolt is too slow to iterate with
    # (FD-32 measurement 2). The debug runtime and iterator level stay the same as RavenCore's.
    if(RAVEN_JOLT_OPTIMIZE_DEBUG AND MSVC)
        set(CMAKE_CXX_FLAGS_DEBUG "/O2 /Ob2")
    elseif(RAVEN_JOLT_OPTIMIZE_DEBUG)
        set(CMAKE_CXX_FLAGS_DEBUG "-O2")
    endif()

    add_subdirectory("${RAVEN_JOLT_ROOT}/Build" "${CMAKE_BINARY_DIR}/ThirdParty/Jolt" EXCLUDE_FROM_ALL)

    set_target_properties(Jolt PROPERTIES
        MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}"
        # Program-database debug information: Edit and Continue (/ZI) conflicts with the optimized Debug flags.
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>"
        POSITION_INDEPENDENT_CODE ON
        FOLDER ThirdParty)
    # Jolt's headers are third-party code to RavenCore: no project warnings from them.
    get_target_property(include_directories Jolt INTERFACE_INCLUDE_DIRECTORIES)
    set_property(TARGET Jolt APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${include_directories})
endfunction()
