# Select native engine sources and dependencies for the current platform.
if(WIN32)
    set(raven_native_directory "Windows")
    target_compile_definitions(RavenCore PRIVATE UNICODE _UNICODE)
    target_link_libraries(RavenCore PRIVATE user32 bcrypt)
elseif(APPLE)
    set(raven_native_directory "Mac")
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
    target_link_libraries(RavenCore PRIVATE PkgConfig::XCB)
else()
    message(FATAL_ERROR "A window backend is not implemented for this platform yet")
endif()

set(raven_native_sources "${raven_private_files}")
list(FILTER raven_native_sources INCLUDE REGEX
    "/Private/Platform/${raven_native_directory}/.*\\.(cpp|hpp|mm)$")
target_sources(RavenCore PRIVATE ${raven_native_sources})
if(NOT WIN32)
    target_sources(RavenCore PRIVATE "Private/Platform/UnsupportedGameModule.cpp")
endif()
