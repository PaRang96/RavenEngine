# Shared by the normal build and the development watcher.
execute_process(
    COMMAND "${DXC}" -spirv -fspv-target-env=vulkan1.0 -WX "${OPTIMIZATION}"
        -T "${PROFILE}" -E "${ENTRY}" "${SOURCE}" -Fo "${OUTPUT}"
    RESULT_VARIABLE compile_result
)
if(NOT compile_result STREQUAL "0")
    message(FATAL_ERROR "DXC failed (${compile_result})")
endif()
execute_process(
    COMMAND "${VALIDATOR}" --target-env vulkan1.0 "${OUTPUT}"
    RESULT_VARIABLE validation_result
)
if(NOT validation_result STREQUAL "0")
    message(FATAL_ERROR "SPIR-V validation failed (${validation_result})")
endif()
