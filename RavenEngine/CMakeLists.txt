set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(RavenCore SHARED
    # Headers shared with engine clients.
    "Public/Raven/Assets/Image.hpp"
    "Public/Raven/Core/Api.hpp"
    "Public/Raven/Core/Application.hpp"
    "Public/Raven/Core/ApplicationClient.hpp"
    "Public/Raven/Core/Assert.hpp"
    "Public/Raven/Core/Files.hpp"
    "Public/Raven/Core/FrameTime.hpp"
    "Public/Raven/Core/Log.hpp"
    "Public/Raven/Core/Paths.hpp"
    "Public/Raven/Math/Transform.hpp"
    "Public/Raven/Platform/Input.hpp"
    "Public/Raven/Platform/Window.hpp"
    "Public/Raven/Renderer/ColorVertex.hpp"
    "Public/Raven/Renderer/PerspectiveCamera.hpp"
    "Public/Raven/Renderer/TextureDesc.hpp"
    "Public/Raven/Renderer/Vulkan/VulkanContext.hpp"
    "Public/Raven/Renderer/Vulkan/VulkanDraw.hpp"

    # Engine implementation and internal headers.
    "Private/Assets/Image.cpp"
    "Private/Core/Application.cpp"
    "Private/Core/Files.cpp"
    "Private/Core/FrameClock.hpp"
    "Private/Core/Log.cpp"
    "Private/Core/Paths.cpp"
    "Private/Core/Paths.hpp"
    "Private/Core/PathText.hpp"
    "Private/Platform/PlatformPaths.hpp"
    "Private/Renderer/Shaders/Color.frag.hlsl"
    "Private/Renderer/Shaders/Color.vert.hlsl"
    "Private/Renderer/Vulkan/VulkanBuffer.cpp"
    "Private/Renderer/Vulkan/VulkanBuffer.hpp"
    "Private/Renderer/Vulkan/VulkanClearPass.cpp"
    "Private/Renderer/Vulkan/VulkanClearPass.hpp"
    "Private/Renderer/Vulkan/VulkanColorPipeline.cpp"
    "Private/Renderer/Vulkan/VulkanColorPipeline.hpp"
    "Private/Renderer/Vulkan/VulkanContext.cpp"
    "Private/Renderer/Vulkan/VulkanDebugMessenger.cpp"
    "Private/Renderer/Vulkan/VulkanDebugMessenger.hpp"
    "Private/Renderer/Vulkan/VulkanDepthAttachment.cpp"
    "Private/Renderer/Vulkan/VulkanDepthAttachment.hpp"
    "Private/Renderer/Vulkan/VulkanDevice.cpp"
    "Private/Renderer/Vulkan/VulkanDevice.hpp"
    "Private/Renderer/Vulkan/VulkanInstance.cpp"
    "Private/Renderer/Vulkan/VulkanInstance.hpp"
    "Private/Renderer/Vulkan/VulkanMesh.cpp"
    "Private/Renderer/Vulkan/VulkanMesh.hpp"
    "Private/Renderer/Vulkan/VulkanPhysicalDevice.cpp"
    "Private/Renderer/Vulkan/VulkanPhysicalDevice.hpp"
    "Private/Renderer/Vulkan/VulkanQueueFamilies.cpp"
    "Private/Renderer/Vulkan/VulkanQueueFamilies.hpp"
    "Private/Renderer/Vulkan/VulkanShaderModule.cpp"
    "Private/Renderer/Vulkan/VulkanShaderModule.hpp"
    "Private/Renderer/Vulkan/VulkanSurface.cpp"
    "Private/Renderer/Vulkan/VulkanSurface.hpp"
    "Private/Renderer/Vulkan/VulkanSwapchain.cpp"
    "Private/Renderer/Vulkan/VulkanSwapchain.hpp"
    "Private/Renderer/Vulkan/VulkanTexture.cpp"
    "Private/Renderer/Vulkan/VulkanTexture.hpp"
)

target_compile_definitions(RavenCore PRIVATE
    RAVEN_CORE_EXPORTS
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:RAVEN_DEVELOPMENT>
    $<$<CONFIG:Debug>:RAVEN_ENABLE_VALIDATION>
)
target_include_directories(RavenCore PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
    ${Vulkan_INCLUDE_DIRS}
)
target_include_directories(RavenCore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_include_directories(RavenCore SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/ThirdParty/stb")

target_compile_features(RavenCore PUBLIC cxx_std_20)
target_link_libraries(RavenCore PRIVATE Vulkan::Vulkan)

add_executable(RavenEngine
    "Private/Samples/RavenEngine.cpp"
    "Private/Samples/RavenEngine.h"
    "Private/Samples/PerspectiveSample.cpp"
    "Private/Samples/PerspectiveSample.hpp"
)
target_include_directories(RavenEngine PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private/Samples")
target_link_libraries(RavenEngine PRIVATE RavenCore)

# Export only declared RAVEN_API entry points. Both modules use the same dynamic CRT.
set_target_properties(RavenCore PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
if(MSVC)
    set_target_properties(RavenCore RavenEngine PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

set_source_files_properties(
    "Private/Renderer/Shaders/Color.vert.hlsl"
    "Private/Renderer/Shaders/Color.frag.hlsl"
    PROPERTIES HEADER_FILE_ONLY TRUE
)

find_program(RAVEN_DXC_EXECUTABLE
    NAMES dxc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        "${Vulkan_INCLUDE_DIR}/../Bin" "${Vulkan_INCLUDE_DIR}/../bin"
    DOC "DXC compiler with SPIR-V support"
    REQUIRED
)
find_program(RAVEN_SPIRV_VALIDATOR_EXECUTABLE
    NAMES spirv-val
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        "${Vulkan_INCLUDE_DIR}/../Bin" "${Vulkan_INCLUDE_DIR}/../bin"
    DOC "SPIRV-Tools validator"
    REQUIRED
)

set(RAVEN_SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Shaders/$<CONFIG>")

function(raven_compile_shader source profile entry output)
    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/Private/Renderer/Shaders/${source}")
    set(shader_output "${RAVEN_SHADER_OUTPUT_DIRECTORY}/${output}")

    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RAVEN_SHADER_OUTPUT_DIRECTORY}"
        COMMAND "${RAVEN_DXC_EXECUTABLE}"
            -spirv -fspv-target-env=vulkan1.0 -WX
            "$<IF:$<CONFIG:Debug>,-Od,-O3>"
            -T "${profile}" -E "${entry}"
            "${shader_source}" -Fo "${shader_output}"
        COMMAND "${RAVEN_SPIRV_VALIDATOR_EXECUTABLE}"
            --target-env vulkan1.0 "${shader_output}"
        DEPENDS "${shader_source}" "${RAVEN_DXC_EXECUTABLE}"
            "${RAVEN_SPIRV_VALIDATOR_EXECUTABLE}"
        COMMENT "Compile and validate ${source} (${entry})"
        VERBATIM
    )
endfunction()

raven_compile_shader("Color.vert.hlsl" vs_6_0 VSMain "Color.vert.spv")
raven_compile_shader("Color.frag.hlsl" ps_6_0 PSMain "Color.frag.spv")

add_custom_target(RavenShaders DEPENDS
    "${RAVEN_SHADER_OUTPUT_DIRECTORY}/Color.vert.spv"
    "${RAVEN_SHADER_OUTPUT_DIRECTORY}/Color.frag.spv"
)

# Run on every build so removed runtime files are restored without relinking the sample.
add_custom_target(RavenContent
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:RavenEngine>/Content/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${RAVEN_SHADER_OUTPUT_DIRECTORY}/Color.vert.spv"
        "${RAVEN_SHADER_OUTPUT_DIRECTORY}/Color.frag.spv"
        "$<TARGET_FILE_DIR:RavenEngine>/Content/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:RavenEngine>/Licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_SOURCE_DIR}/ThirdParty/stb/LICENSE"
        "$<TARGET_FILE_DIR:RavenEngine>/Licenses/stb-LICENSE.txt"
    DEPENDS RavenShaders
    COMMENT "Stage validated shaders and dependency license"
    VERBATIM
)
add_dependencies(RavenCore RavenContent)

if(WIN32)
    target_sources(RavenCore PRIVATE
        "Private/Platform/Windows/Win32Paths.cpp"
        "Private/Platform/Windows/Win32Window.cpp"
        "Private/Platform/Windows/Win32Window.hpp"
    )
    target_compile_definitions(RavenCore PRIVATE UNICODE _UNICODE)
    target_link_libraries(RavenCore PRIVATE user32)
elseif(APPLE)
    enable_language(OBJCXX)
    set_target_properties(RavenCore PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON
        OBJCXX_EXTENSIONS OFF
    )
    target_sources(RavenCore PRIVATE
        "Private/Platform/Mac/MacPaths.cpp"
        "Private/Platform/Mac/CocoaWindow.mm"
        "Private/Platform/Mac/CocoaWindow.hpp"
    )
    target_compile_options(RavenCore PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
    target_compile_definitions(RavenCore PRIVATE _UNICODE)
    target_link_libraries(RavenCore PRIVATE ${COCOA_LIBRARY} ${QUARTZCORE_LIBRARY})
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xkb)
    target_sources(RavenCore PRIVATE
        "Private/Platform/Linux/LinuxPaths.cpp"
        "Private/Platform/Linux/XcbWindow.cpp"
        "Private/Platform/Linux/XcbWindow.hpp"
    )
    target_link_libraries(RavenCore PRIVATE PkgConfig::XCB)
else()
    message(FATAL_ERROR "A window backend is not implemented for this platform yet")
endif()
