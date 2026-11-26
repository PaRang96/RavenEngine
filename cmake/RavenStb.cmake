# stb_image 2.30: the PNG decoder behind Private/Assets/Image.cpp (docs/ImageLoading.md).
#
# ThirdParty/stb is an unmodified upstream copy, replaced only by scripts/UpdateStb.cmake. Every Raven
# setting lives in this file and in Image.cpp's one implementation block; only Image.cpp includes it.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_STB_IMAGE_VERSION 2.30)
set(RAVEN_STB_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/stb")

# Builds the header-only RavenStb interface target.
function(raven_add_stb)
    file(STRINGS "${RAVEN_STB_ROOT}/stb_image.h" line LIMIT_COUNT 1 REGEX "stb_image - v[0-9.]+")
    if(NOT line MATCHES "v([0-9]+\\.[0-9]+)")
        message(FATAL_ERROR "Cannot read the version from ThirdParty/stb/stb_image.h")
    endif()
    raven_check_version(stb_image ${RAVEN_STB_IMAGE_VERSION} "${CMAKE_MATCH_1}")
    add_library(RavenStb INTERFACE)
    target_include_directories(RavenStb SYSTEM INTERFACE "${RAVEN_STB_ROOT}")
endfunction()
