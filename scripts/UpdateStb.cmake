# Replaces ThirdParty/stb with stb_image.h and the license of an stb commit (ThirdParty/stb/README.md).
# The copy is never edited by hand; every Raven setting lives in cmake/RavenStb.cmake and Image.cpp's
# implementation block, so an update is this script plus the version pin there.
#
#   cmake -DSOURCE=<folder holding stb_image.h and LICENSE> -P scripts/UpdateStb.cmake

cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<folder holding stb_image.h and LICENSE>")
endif()
file(STRINGS "${SOURCE}/stb_image.h" line LIMIT_COUNT 1 REGEX "stb_image - v[0-9.]+")
if(NOT line MATCHES "v([0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "Cannot read the version from ${SOURCE}/stb_image.h")
endif()
set(version "${CMAKE_MATCH_1}")
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(stb "${SOURCE}" VERSION ${version} stb_image.h LICENSE)
message(STATUS "Next: set RAVEN_STB_IMAGE_VERSION to ${version} in cmake/RavenStb.cmake and record the commit in ThirdParty/stb/README.md")
