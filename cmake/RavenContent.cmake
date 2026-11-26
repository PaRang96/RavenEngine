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
set(RAVEN_SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Private/Renderer/Shaders")
file(GLOB_RECURSE RAVEN_SHADER_INCLUDES CONFIGURE_DEPENDS "${RAVEN_SHADER_DIRECTORY}/*.hlsli")
function(raven_add_shader source profile entry)
    set(shader_source "${RAVEN_SHADER_DIRECTORY}/${source}")
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
            "-DSOURCE=${shader_source}" "-DOUTPUT=${shader_output}" "-DINCLUDE=${RAVEN_SHADER_DIRECTORY}"
            -P "${RAVEN_SHADER_COMPILE_SCRIPT}"
        DEPENDS "${shader_source}" ${RAVEN_SHADER_INCLUDES} "${RAVEN_DXC_EXECUTABLE}"
            "${RAVEN_SPIRV_VALIDATOR_EXECUTABLE}" "${RAVEN_SHADER_COMPILE_SCRIPT}"
        COMMENT "Compile and validate ${source} (${entry})"
        VERBATIM
    )
    set(outputs "${RAVEN_SHADER_OUTPUTS}")
    list(APPEND outputs "${shader_output}")
    set(RAVEN_SHADER_OUTPUTS "${outputs}" PARENT_SCOPE)
endfunction()

# Renderer/Shaders/ShaderLibrary.cpp lists the same programs for live reload (docs/HotReloadDesign.md, H2).
raven_add_shader("Color.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("Color.frag.hlsl" ps_6_0 PSMain)
# Debug views over the lit meshes (docs/HotReloadDesign.md, H3).
raven_add_shader("Unlit.frag.hlsl" ps_6_0 PSMain)
raven_add_shader("Normals.frag.hlsl" ps_6_0 PSMain)
raven_add_shader("Wireframe.frag.hlsl" ps_6_0 PSMain)
raven_add_shader("Texture.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("Texture.frag.hlsl" ps_6_0 PSMain)
# GPU-driven scene passes (docs/GpuDrivenDesign.md, G2/G5).
raven_add_shader("Scatter.comp.hlsl" cs_6_0 CSMain)
raven_add_shader("Cull.comp.hlsl" cs_6_0 CSMain)
# Developer debug lines: the collider view (docs/GameServicesDesign.md, FD-32).
raven_add_shader("DebugLine.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("DebugLine.frag.hlsl" ps_6_0 PSMain)
# The shared 2D path: UI, text and on-screen diagnostics (docs/UIDesign.md, section 4).
raven_add_shader("Overlay.vert.hlsl" vs_6_0 VSMain)
raven_add_shader("Overlay.frag.hlsl" ps_6_0 PSMain)
foreach(include IN LISTS RAVEN_SHADER_INCLUDES)
    target_sources(RavenCore PRIVATE "${include}")
    set_source_files_properties("${include}" PROPERTIES HEADER_FILE_ONLY TRUE)
endforeach()
include("${PROJECT_SOURCE_DIR}/cmake/RavenBuildEnvironment.cmake")
raven_utf8_literal("${CMAKE_CURRENT_SOURCE_DIR}/Private/Renderer/Shaders" RAVEN_SHADER_SOURCE_LITERAL)
raven_utf8_literal("${CMAKE_COMMAND}" RAVEN_SHADER_CMAKE_LITERAL)
raven_utf8_literal("${RAVEN_SHADER_COMPILE_SCRIPT}" RAVEN_SHADER_SCRIPT_LITERAL)
raven_utf8_literal("${RAVEN_DXC_EXECUTABLE}" RAVEN_SHADER_DXC_LITERAL)
raven_utf8_literal("${RAVEN_SPIRV_VALIDATOR_EXECUTABLE}" RAVEN_SHADER_VALIDATOR_LITERAL)
configure_file("${PROJECT_SOURCE_DIR}/cmake/ShaderWatchConfig.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/ShaderWatchConfig.hpp.in" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/$<CONFIG>/ShaderWatchConfig.hpp"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/Generated/ShaderWatchConfig.hpp.in")
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
raven_stage_file("ThirdParty/stb/LICENSE" "Licenses/stb-LICENSE.txt")
raven_stage_file("ThirdParty/ufbx/LICENSE" "Licenses/ufbx-LICENSE.txt")
raven_stage_file("ThirdParty/JoltPhysics/LICENSE" "Licenses/JoltPhysics-LICENSE.txt")
raven_stage_file("ThirdParty/miniaudio/LICENSE" "Licenses/miniaudio-LICENSE.txt")
raven_stage_file("ThirdParty/stb_vorbis/LICENSE" "Licenses/stb_vorbis-LICENSE.txt")
raven_stage_file("ThirdParty/FreeType/docs/FTL.TXT" "Licenses/FreeType-LICENSE.txt")
raven_stage_file("ThirdParty/HarfBuzz/COPYING" "Licenses/HarfBuzz-COPYING.txt")
raven_stage_file("ThirdParty/msdfgen/LICENSE.txt" "Licenses/msdfgen-LICENSE.txt")
raven_stage_file("ThirdParty/msdf-atlas-gen/LICENSE.txt" "Licenses/msdf-atlas-gen-LICENSE.txt")
raven_stage_file("ThirdParty/Yoga/LICENSE" "Licenses/Yoga-LICENSE.txt")
raven_stage_file("ThirdParty/icu4c/LICENSE" "Licenses/ICU-LICENSE.txt")
# The text stack's engine content (docs/UIDesign.md): ICU's filtered data and the default fonts.
raven_stage_file("ThirdParty/icu4c/data/icudt79l.dat" "Content/ICU/icudt79l.dat")
foreach(font NotoSans NotoSansArabic NotoSansKR)
    raven_stage_file("Content/Fonts/${font}.ttf" "Content/Fonts/${font}.ttf")
    raven_stage_file("Content/Fonts/${font}-OFL.txt" "Licenses/${font}-OFL.txt")
endforeach()
set(RAVEN_PLAYER_SUPPORT_SOURCES "${RAVEN_CONTENT_SOURCES}")
set(RAVEN_PLAYER_SUPPORT_DESTINATIONS "${RAVEN_CONTENT_DESTINATIONS}")

# Each game root's content enrolls automatically and keeps its Games/<Name>/Content path, which
# --no-watch runs read; development reads the source folders directly.
file(GLOB_RECURSE raven_runtime_content CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${PROJECT_SOURCE_DIR}/Games/*/Content/*")
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
