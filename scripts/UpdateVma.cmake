# Replaces ThirdParty/VulkanMemoryAllocator with vk_mem_alloc.h and the license of a VMA release
# (ThirdParty/VulkanMemoryAllocator/README.md). The copy is never edited by hand; every Raven setting
# lives in cmake/RavenVma.cmake and VulkanMemory.cpp's implementation block, so an update is this
# script plus the version pin there.
#
#   cmake -DSOURCE=<folder holding include/vk_mem_alloc.h and LICENSE.txt> -P scripts/UpdateVma.cmake

cmake_minimum_required(VERSION 3.28)
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<folder holding include/vk_mem_alloc.h and LICENSE.txt>")
endif()
file(STRINGS "${SOURCE}/include/vk_mem_alloc.h" line LIMIT_COUNT 1 REGEX "#define VMA_VERSION")
if(NOT line MATCHES "VK_MAKE_VERSION\\(([0-9]+), ([0-9]+), ([0-9]+)\\)")
    message(FATAL_ERROR "Cannot read the version from ${SOURCE}/include/vk_mem_alloc.h")
endif()
set(version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(VulkanMemoryAllocator "${SOURCE}" VERSION ${version} include/vk_mem_alloc.h LICENSE.txt)
message(STATUS "Next: set RAVEN_VMA_VERSION to ${version} in cmake/RavenVma.cmake and record the commit in ThirdParty/VulkanMemoryAllocator/README.md")
