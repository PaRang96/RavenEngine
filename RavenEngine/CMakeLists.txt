set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(RavenEngine
    "RavenEngine.cpp"
    "RavenEngine.h"
    "Core/Application.cpp"
    "Core/Application.hpp"
    "Core/Assert.hpp"
    "Core/Log.cpp"
    "Core/Log.hpp"
    "Platform/Window.hpp"
    "Platform/Input.hpp"
    "Renderer/Vulkan/VulkanDebugMessenger.cpp"
    "Renderer/Vulkan/VulkanDebugMessenger.hpp"
    "Renderer/Vulkan/VulkanInstance.cpp"
    "Renderer/Vulkan/VulkanInstance.hpp"
    "Renderer/Vulkan/VulkanSurface.cpp"
    "Renderer/Vulkan/VulkanSurface.hpp"
    "Renderer/Vulkan/VulkanQueueFamilies.cpp"
    "Renderer/Vulkan/VulkanQueueFamilies.hpp"
    "Renderer/Vulkan/VulkanPhysicalDevice.hpp"
    "Renderer/Vulkan/VulkanPhysicalDevice.cpp"
)

target_compile_definitions(RavenEngine PRIVATE
    $<$<CONFIG:Debug>:RAVEN_ENABLE_VALIDATION>
)
target_link_libraries(RavenEngine PRIVATE Vulkan::Vulkan)

if(WIN32)
    target_sources(RavenEngine PRIVATE
        "Platform/Windows/Win32Window.cpp"
        "Platform/Windows/Win32Window.hpp"
    )
    target_compile_definitions(RavenEngine PRIVATE UNICODE _UNICODE)
    target_link_libraries(RavenEngine PRIVATE user32)
elseif(APPLE)
    enable_language(OBJCXX)
    set_target_properties(RavenEngine PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON
        OBJCXX_EXTENSIONS OFF
    )
    target_sources(RavenEngine PRIVATE
        "Platform/Mac/Arm64Mac.mm"
        "Platform/Mac/Arm64Mac.hpp"
    )
    target_compile_options(RavenEngine PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    target_compile_definitions(RavenEngine PRIVATE _UNICODE)
    target_link_libraries(RavenEngine PRIVATE ${COCOA_LIBRARY} ${QUARTZCORE_LIBRARY})
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xkb)
    target_sources(RavenEngine PRIVATE
        "Platform/Linux/XcbWindow.cpp"
        "Platform/Linux/XcbWindow.hpp"
    )
    target_link_libraries(RavenEngine PRIVATE PkgConfig::XCB)
else()
    message(FATAL_ERROR "A window backend is not implemented for this platform yet")
endif()
