# Shared by the normal build and the development watcher.
# INCLUDE names the engine shader directory, which holds GpuScene.hlsli and Raven/Material.hlsli.
set(include_arguments)
if(DEFINED INCLUDE)
    set(include_arguments -I "${INCLUDE}")
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
