# Replaces ThirdParty/ufbx with the files of a ufbx release (ThirdParty/ufbx/README.md). The copy is
# never edited by hand; every Raven setting lives in cmake/RavenUfbx.cmake, so an update is this script
# plus the version pin there.
#
#   cmake -DSOURCE=<extracted ufbx release folder> -P scripts/UpdateUfbx.cmake

cmake_minimum_required(VERSION 3.28)
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<folder holding ufbx.c, ufbx.h and LICENSE>")
endif()
file(STRINGS "${SOURCE}/ufbx.h" line REGEX "^#define UFBX_HEADER_VERSION ufbx_pack_version\\(")
if(NOT line MATCHES "ufbx_pack_version\\(([0-9]+), *([0-9]+), *([0-9]+)\\)")
    message(FATAL_ERROR "Cannot read UFBX_HEADER_VERSION from ${SOURCE}/ufbx.h")
endif()
set(version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(ufbx "${SOURCE}" VERSION ${version} ufbx.c ufbx.h LICENSE)
message(STATUS "Next: set RAVEN_UFBX_VERSION to ${version} in cmake/RavenUfbx.cmake and record the release in ThirdParty/ufbx/README.md")
