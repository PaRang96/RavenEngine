# Select native engine sources and dependencies for the current platform.
if(WIN32)
    set(raven_native_directory "Windows")
    target_compile_definitions(RavenCore PRIVATE UNICODE _UNICODE)
    target_link_libraries(RavenCore PRIVATE user32 bcrypt ole32 shell32)
    if(MSVC AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
        set(raven_player_platform "windows-x64")
        set(raven_player_readme "${PROJECT_SOURCE_DIR}/docs/WindowsPlayerReadme.txt")
    endif()
elseif(APPLE)
    set(raven_native_directory "Mac")
    set(raven_platform_fallbacks FallbackBuildProcess FallbackGameModule FallbackKeyNames PollDirectoryWatch)
    target_compile_options(RavenCore PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    target_compile_definitions(RavenCore PRIVATE _UNICODE)
    target_link_libraries(RavenCore PRIVATE ${COCOA_LIBRARY} ${QUARTZCORE_LIBRARY})

    # The loader opens the validation layer dynamically; Homebrew's manifest
    # names only the dylib, so Debug builds need its directory in the runpath.
    find_library(RAVEN_VULKAN_VALIDATION_LIBRARY
        NAMES VkLayer_khronos_validation
        HINTS "$ENV{VULKAN_SDK}/lib" "${Vulkan_INCLUDE_DIR}/../lib"
    )
    if(RAVEN_VULKAN_VALIDATION_LIBRARY)
        get_filename_component(RAVEN_VULKAN_VALIDATION_LIBRARY_DIRECTORY
            "${RAVEN_VULKAN_VALIDATION_LIBRARY}" DIRECTORY)
        set_property(TARGET RavenCore RavenEngine APPEND PROPERTY BUILD_RPATH
            "$<$<CONFIG:Debug>:${RAVEN_VULKAN_VALIDATION_LIBRARY_DIRECTORY}>"
        )
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xkb)
    set(raven_native_directory "Linux")
    set(raven_platform_fallbacks FallbackBuildProcess FallbackGameModule FallbackKeyNames PollDirectoryWatch)
    target_link_libraries(RavenCore PRIVATE PkgConfig::XCB)
else()
    message(FATAL_ERROR "A window backend is not implemented for this platform yet")
endif()

# The platform folders this OS builds (docs/PlatformLayerDesign.md): its own, and shared POSIX
# code off Windows. Then one fallback per area the OS has no backend for yet, listed in its branch
# above; a backend that lands removes its entry and sets its capability.
set(raven_platform_folders "${raven_native_directory}")
if(NOT WIN32)
    list(APPEND raven_platform_folders Posix)
endif()
foreach(folder IN LISTS raven_platform_folders)
    set(raven_folder_sources "${raven_private_files}")
    list(FILTER raven_folder_sources INCLUDE REGEX "/Private/Platform/${folder}/.*\\.(cpp|hpp|mm)$")
    target_sources(RavenCore PRIVATE ${raven_folder_sources})
endforeach()
foreach(fallback IN LISTS raven_platform_fallbacks)
    target_sources(RavenCore PRIVATE "Private/Platform/Fallback/${fallback}.cpp")
endforeach()
# Platform/<OS>/PlatformCapabilitiesOs.hpp, included by Platform/PlatformCapabilities.hpp.
target_include_directories(RavenCore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private/Platform/${raven_native_directory}")
