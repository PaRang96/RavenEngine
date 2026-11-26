# Replaces ThirdParty/HarfBuzz with the files of a HarfBuzz release (ThirdParty/HarfBuzz/README.md). The copy is
# never edited by hand; every Raven setting lives in its cmake/Raven<Lib>.cmake, so an update is this
# script plus the version pin there.
#
#   cmake -DSOURCE=<the extracted harfbuzz-<version>.tar.xz from github.com/harfbuzz/harfbuzz/releases> -P scripts/UpdateHarfBuzz.cmake

cmake_minimum_required(VERSION 3.28)
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<the extracted HarfBuzz release>")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(HarfBuzz "${SOURCE}" src COPYING README.md AUTHORS)
message(STATUS "Next: set RAVEN_HARFBUZZ_VERSION in cmake/RavenHarfBuzz.cmake, record the release in ThirdParty/HarfBuzz/README.md, and build")
