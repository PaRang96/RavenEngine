# Checks that platform code stays encapsulated (planning decision 21; docs/PlatformLayerDesign.md):
# no OS or compiler macro and no OS header outside the platform folders under
# RavenEngine/Private/Platform/. The one allowed exception is RavenEngine/Public/Raven/Core/Api.hpp,
# whose export macros must name the OS. A check, not a test: CI runs it as one step.
#
#   cmake -P scripts/CheckPlatformCode.cmake

cmake_minimum_required(VERSION 3.28)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(allowed "RavenEngine/Public/Raven/Core/Api.hpp")
set(macros "_WIN32|_WIN64|__APPLE__|__linux__|__unix__|__ANDROID__|_MSC_VER|__clang__|__GNUC__|__MINGW32__")
set(os_headers "Windows|windows|winsock2|shlobj|shellapi|dwmapi|unistd|dlfcn|pthread|xcb/[^>]*|X11/[^>]*|Cocoa/[^>]*|AppKit/[^>]*|objc/[^>]*|mach/[^>]*|sys/[^>]*")
set(macro_pattern "(^|[^A-Za-z0-9_])(${macros})([^A-Za-z0-9_]|$)")
set(header_pattern "^[ \t]*#[ \t]*(include|import)[ \t]*[<\"](${os_headers})\\.h[>\"]")

# Tracked files only: build trees and local probes are not the engine's code.
execute_process(COMMAND git ls-files -- "RavenEngine/*.cpp" "RavenEngine/*.hpp" "RavenEngine/*.h" "RavenEngine/*.mm"
        "RavenEngine/*.inl" "Games/*.cpp" "Games/*.hpp" "Tests/*.cpp" "Tests/*.hpp" "scripts/*.cpp"
    WORKING_DIRECTORY "${repository}" OUTPUT_VARIABLE candidates RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "git ls-files failed")
endif()
string(REGEX REPLACE "\n$" "" candidates "${candidates}")
string(REPLACE "\n" ";" candidates "${candidates}")
set(problems "")
set(checked 0)
foreach(file IN LISTS candidates)
    # The platform folders own their OS: Private/Platform/<folder>/... The shared area headers at
    # the top of Private/Platform are checked like any other shared code.
    if(file MATCHES "^RavenEngine/Private/Platform/[^/]+/" OR file IN_LIST allowed)
        continue()
    endif()
    math(EXPR checked "${checked} + 1")
    file(STRINGS "${repository}/${file}" lines REGEX "${macro_pattern}|${header_pattern}")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        list(APPEND problems "${file}: ${line}")
    endforeach()
endforeach()
if(problems)
    list(JOIN problems "\n  " text)
    message(FATAL_ERROR "Platform code must stay inside RavenEngine/Private/Platform/<folder>/ "
        "(docs/PlatformLayerDesign.md); move it behind a platform area or a capability:\n  ${text}")
endif()
message(STATUS "Platform code: ${checked} shared files name no OS")
