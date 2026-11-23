# Shared defaults for the host and independent game-DLL projects.
include_guard(GLOBAL)

# Some CMake versions map C++23 to MSVC's working-draft mode.
# Teach CMake the supported C++23 flag so it emits a single standard option.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND
    CMAKE_CXX23_STANDARD_COMPILE_OPTION MATCHES "c[+][+]latest$")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-std:c++23" RAVEN_MSVC_HAS_CXX23)
    if(RAVEN_MSVC_HAS_CXX23)
        set(raven_cxx23_option "-std:c++23")
    else()
        check_cxx_compiler_flag("-std:c++23preview" RAVEN_MSVC_HAS_CXX23_PREVIEW)
        if(NOT RAVEN_MSVC_HAS_CXX23_PREVIEW)
            message(FATAL_ERROR "RavenEngine requires MSVC with C++23 support (Visual Studio 2022 17.13 or newer)")
        endif()
        set(raven_cxx23_option "-std:c++23preview")
    endif()
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "${raven_cxx23_option}")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "${raven_cxx23_option}")
    unset(raven_cxx23_option)
endif()

if(APPLE)
    enable_language(OBJCXX)
    # Older CMake releases omit the Objective-C++23 mapping for AppleClang.
    # The same AppleClang release accepts the C++23 flag for both languages.
    if(CMAKE_OBJCXX_COMPILER_ID STREQUAL "AppleClang" AND
        CMAKE_CXX_COMPILER_ID STREQUAL CMAKE_OBJCXX_COMPILER_ID AND
        CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL CMAKE_OBJCXX_COMPILER_VERSION AND
        NOT DEFINED CMAKE_OBJCXX23_STANDARD_COMPILE_OPTION AND
        DEFINED CMAKE_CXX23_STANDARD_COMPILE_OPTION)
        set(CMAKE_OBJCXX23_STANDARD_COMPILE_OPTION "${CMAKE_CXX23_STANDARD_COMPILE_OPTION}")
        set(CMAKE_OBJCXX23_EXTENSION_COMPILE_OPTION "${CMAKE_CXX23_EXTENSION_COMPILE_OPTION}")
    endif()
endif()

foreach(raven_language IN ITEMS CXX OBJCXX)
    set(CMAKE_${raven_language}_STANDARD 23)
    set(CMAKE_${raven_language}_STANDARD_REQUIRED ON)
    set(CMAKE_${raven_language}_EXTENSIONS OFF)
endforeach()
unset(raven_language)

# Every module uses the same dynamic CRT and explicit export annotations.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

# Private, stable standard-library headers; project/header edits do not rebuild
# the PCH. Each target owns its artifact because their export definitions differ.
# Extra arguments add target-specific standard headers.
option(RAVEN_USE_PCH "Precompile standard-library headers for engine, sample and game builds" ON)
function(raven_enable_precompiled_headers target)
    if(RAVEN_USE_PCH)
        target_precompile_headers(${target} PRIVATE
            <algorithm> <array> <chrono> <cstdint> <filesystem> <memory>
            <span> <string> <string_view> <unordered_map> <utility> <vector> ${ARGN})
    endif()
endfunction()
