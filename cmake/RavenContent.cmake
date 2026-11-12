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
function(raven_add_shader source profile entry)
    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/Private/Renderer/Shaders/${source}")
    get_filename_component(shader_name "${source}" NAME_WLE)
    set(shader_output "${RAVEN_SHADER_OUTPUT_DIRECTORY}/${shader_name}.spv")
    target_sources(RavenCore PRIVATE "${shader_source}")
    set_source_files_properties("${shader_source}" PROPERTIES HEADER_FILE_ONLY TRUE)
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
    set(outputs "${RAVEN_SHADER_OUTPUTS}")
    list(APPEND outputs "${shader_output}")
    set(RAVEN_SHADER_OUTPUTS "${outputs}" PARENT_SCOPE)
endfunction()

raven_add_shader("Color.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("Color.frag.hlsl" ps_6_0 PSMain)
raven_add_shader("Texture.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("Texture.frag.hlsl" ps_6_0 PSMain)
add_custom_target(RavenShaders DEPENDS ${RAVEN_SHADER_OUTPUTS})

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
raven_stage_file("Content/Textures/UVGrid.png")
raven_stage_file("Content/Textures/UVGridAlternate.png")
raven_stage_file("ThirdParty/stb/LICENSE" "Licenses/stb-LICENSE.txt")

# One process restores missing runtime files without rewriting identical files.
configure_file("${PROJECT_SOURCE_DIR}/cmake/StageContent.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/StageContent.cmake.in" @ONLY)
set(RAVEN_CONTENT_BUILD_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/StageContent-$<CONFIG>.cmake")
file(GENERATE OUTPUT "${RAVEN_CONTENT_BUILD_SCRIPT}"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/StageContent.cmake.in")
add_custom_target(RavenContent
    COMMAND "${CMAKE_COMMAND}" -P "${RAVEN_CONTENT_BUILD_SCRIPT}"
    DEPENDS RavenShaders ${RAVEN_CONTENT_SOURCES}
    COMMENT "Stage validated shaders, sample textures and dependency license"
    VERBATIM
)
add_dependencies(RavenCore RavenContent)
