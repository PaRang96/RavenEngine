# Shared by the normal build and the development watcher.
# INCLUDE names the engine shader directory, which holds GpuScene.hlsli and Raven/Material.hlsli.
# DEPENDENCIES (watcher only) names a file that receives DXC's include list, so the watcher can
# recompile a program when any of its includes changes (docs/HotReloadDesign.md, H2).
set(include_arguments)
if(DEFINED INCLUDE)
    set(include_arguments -I "${INCLUDE}")
endif()
if(DEFINED DEPENDENCIES)
    # -MF lists the dependencies without compiling, so it runs as its own pass.
    execute_process(
        COMMAND "${DXC}" -spirv -fspv-target-env=vulkan1.2 ${include_arguments}
            -T "${PROFILE}" -E "${ENTRY}" "${SOURCE}" -MF "${DEPENDENCIES}"
        RESULT_VARIABLE dependency_result
    )
    if(NOT dependency_result STREQUAL "0")
        message(FATAL_ERROR "DXC dependency scan failed (${dependency_result})")
    endif()
endif()
execute_process(
    COMMAND "${DXC}" -spirv -fspv-target-env=vulkan1.2 -WX "${OPTIMIZATION}" ${include_arguments}
        -T "${PROFILE}" -E "${ENTRY}" "${SOURCE}" -Fo "${OUTPUT}"
    RESULT_VARIABLE compile_result
)
if(NOT compile_result STREQUAL "0")
    message(FATAL_ERROR "DXC failed (${compile_result})")
endif()
execute_process(
    COMMAND "${VALIDATOR}" --target-env vulkan1.2 "${OUTPUT}"
    RESULT_VARIABLE validation_result
)
if(NOT validation_result STREQUAL "0")
    message(FATAL_ERROR "SPIR-V validation failed (${validation_result})")
endif()
