# Register each shader/runtime file once; derive target sources, outputs and staging.
set(RAVEN_SHADER_TOOL_HINTS
    "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    "${Vulkan_INCLUDE_DIR}/../Bin" "${Vulkan_INCLUDE_DIR}/../bin"
)
find_program(RAVEN_DXC_EXECUTABLE
    NAMES dxc
    HINTS ${RAVEN_SHADER_TOOL_HINTS}
    DOC "DXC compiler with SPIR-V support"
    REQUIRED
)
find_program(RAVEN_SPIRV_VALIDATOR_EXECUTABLE
    NAMES spirv-val
    HINTS ${RAVEN_SHADER_TOOL_HINTS}
    DOC "SPIRV-Tools validator"
    REQUIRED
)

set(RAVEN_SHADER_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Shaders/$<CONFIG>")
set(RAVEN_SHADER_OUTPUTS)
set(RAVEN_SHADER_COMPILE_SCRIPT "${PROJECT_SOURCE_DIR}/cmake/CompileShader.cmake")
set(RAVEN_SHADER_OPTIMIZATION "$<IF:$<CONFIG:Debug>,-Od,-O3>")
function(raven_add_shader source profile entry)
    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/Private/Renderer/Shaders/${source}")
    get_filename_component(shader_name "${source}" NAME_WLE)
    set(shader_output "${RAVEN_SHADER_OUTPUT_DIRECTORY}/${shader_name}.spv")
    target_sources(RavenCore PRIVATE "${shader_source}")
    set_source_files_properties("${shader_source}" PROPERTIES HEADER_FILE_ONLY TRUE)
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RAVEN_SHADER_OUTPUT_DIRECTORY}"
        COMMAND "${CMAKE_COMMAND}" "-DDXC=${RAVEN_DXC_EXECUTABLE}"
            "-DVALIDATOR=${RAVEN_SPIRV_VALIDATOR_EXECUTABLE}"
            "-DOPTIMIZATION=${RAVEN_SHADER_OPTIMIZATION}" "-DPROFILE=${profile}" "-DENTRY=${entry}"
            "-DSOURCE=${shader_source}" "-DOUTPUT=${shader_output}"
            -P "${RAVEN_SHADER_COMPILE_SCRIPT}"
        DEPENDS "${shader_source}" "${RAVEN_DXC_EXECUTABLE}"
            "${RAVEN_SPIRV_VALIDATOR_EXECUTABLE}" "${RAVEN_SHADER_COMPILE_SCRIPT}"
        COMMENT "Compile and validate ${source} (${entry})"
        VERBATIM
    )
    set(outputs "${RAVEN_SHADER_OUTPUTS}")
    list(APPEND outputs "${shader_output}")
    set(RAVEN_SHADER_OUTPUTS "${outputs}" PARENT_SCOPE)
endfunction()

set(RAVEN_WATCH_PROFILE ps_6_0)
set(RAVEN_WATCH_ENTRY PSMain)
set(RAVEN_LIT_FRAGMENT_SOURCE Color.frag.hlsl)
set(RAVEN_TEXTURED_FRAGMENT_SOURCE Texture.frag.hlsl)
raven_add_shader("Color.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("${RAVEN_LIT_FRAGMENT_SOURCE}" "${RAVEN_WATCH_PROFILE}" "${RAVEN_WATCH_ENTRY}")
raven_add_shader("Texture.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("${RAVEN_TEXTURED_FRAGMENT_SOURCE}" "${RAVEN_WATCH_PROFILE}" "${RAVEN_WATCH_ENTRY}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/ShaderWatchConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/ShaderWatchConfig.hpp.in" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/$<CONFIG>/ShaderWatchConfig.hpp"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/ShaderWatchConfig.hpp.in")
add_custom_target(RavenShaders DEPENDS ${RAVEN_SHADER_OUTPUTS})

configure_file("${PROJECT_SOURCE_DIR}/cmake/ContentSourceConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/ContentSourceConfig.hpp" @ONLY)

set(RAVEN_CONTENT_SOURCES)
set(RAVEN_CONTENT_DESTINATIONS)
# Relative project files keep their path by default; aliases are explicit.
function(raven_stage_file source)
    set(destination "${source}")
    if(ARGC GREATER 1)
        set(destination "${ARGV1}")
    elseif(IS_ABSOLUTE "${source}")
        message(FATAL_ERROR "An absolute staging source requires a destination")
    endif()
    if(NOT IS_ABSOLUTE "${source}")
        set(source "${PROJECT_SOURCE_DIR}/${source}")
    endif()
    set(sources "${RAVEN_CONTENT_SOURCES}")
    set(destinations "${RAVEN_CONTENT_DESTINATIONS}")
    list(APPEND sources "${source}")
    list(APPEND destinations "${destination}")
    set(RAVEN_CONTENT_SOURCES "${sources}" PARENT_SCOPE)
    set(RAVEN_CONTENT_DESTINATIONS "${destinations}" PARENT_SCOPE)
endfunction()

foreach(shader IN LISTS RAVEN_SHADER_OUTPUTS)
    get_filename_component(name "${shader}" NAME)
    raven_stage_file("${shader}" "Content/Shaders/${name}")
endforeach()
raven_stage_file("ThirdParty/stb/LICENSE" "Licenses/stb-LICENSE.txt")
raven_stage_file("ThirdParty/ufbx/LICENSE" "Licenses/ufbx-LICENSE.txt")
set(RAVEN_PLAYER_SUPPORT_SOURCES "${RAVEN_CONTENT_SOURCES}")
set(RAVEN_PLAYER_SUPPORT_DESTINATIONS "${RAVEN_CONTENT_DESTINATIONS}")

# Content and metadata enroll automatically; development reads their source root directly.
file(GLOB_RECURSE raven_runtime_content CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${PROJECT_SOURCE_DIR}/Content/*")
foreach(content IN LISTS raven_runtime_content)
    file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${content}")
    raven_stage_file("${relative}")
endforeach()

# One process restores missing runtime files without rewriting identical files.
configure_file("${PROJECT_SOURCE_DIR}/cmake/StageContent.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/StageContent.cmake.in" @ONLY)
set(RAVEN_CONTENT_BUILD_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/StageContent-$<CONFIG>.cmake")
file(GENERATE OUTPUT "${RAVEN_CONTENT_BUILD_SCRIPT}"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/StageContent.cmake.in")
add_custom_target(RavenContent
    COMMAND "${CMAKE_COMMAND}" -P "${RAVEN_CONTENT_BUILD_SCRIPT}"
    DEPENDS RavenShaders ${RAVEN_CONTENT_SOURCES}
    COMMENT "Stage validated shaders, sample content and dependency license"
    VERBATIM
)
add_dependencies(RavenCore RavenContent)
