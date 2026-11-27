# Vulkan Memory Allocator 3.5.0: GPU memory for textures, staging and attachments
# (docs/RenderingContentDesign.md, FD-24, owner decision 2026-11-26).
#
# ThirdParty/VulkanMemoryAllocator is an unmodified upstream copy, replaced only by
# scripts/UpdateVma.cmake. Every Raven setting lives in this file and in VulkanMemory.cpp's one
# implementation block; only VulkanMemory.cpp includes the header.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_VMA_VERSION 3.5.0)
set(RAVEN_VMA_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/VulkanMemoryAllocator")

# Builds the header-only RavenVma interface target.
function(raven_add_vma)
    file(STRINGS "${RAVEN_VMA_ROOT}/include/vk_mem_alloc.h" line LIMIT_COUNT 1 REGEX "#define VMA_VERSION")
    if(NOT line MATCHES "VK_MAKE_VERSION\\(([0-9]+), ([0-9]+), ([0-9]+)\\)")
        message(FATAL_ERROR "Cannot read the version from ThirdParty/VulkanMemoryAllocator/include/vk_mem_alloc.h")
    endif()
    raven_check_version(VulkanMemoryAllocator ${RAVEN_VMA_VERSION} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    add_library(RavenVma INTERFACE)
    target_include_directories(RavenVma SYSTEM INTERFACE "${RAVEN_VMA_ROOT}/include")
endfunction()
