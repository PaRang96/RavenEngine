# Replaces ThirdParty/Yoga with the files of a Yoga release (ThirdParty/Yoga/README.md). The copy is
# never edited by hand; every Raven setting lives in its cmake/Raven<Lib>.cmake, so an update is this
# script plus the version pin there.
#
#   cmake -DSOURCE=<the extracted the v<version> tag archive of github.com/facebook/yoga> -P scripts/UpdateYoga.cmake

cmake_minimum_required(VERSION 3.28)
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<the extracted Yoga release>")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(Yoga "${SOURCE}" yoga LICENSE README.md)
message(STATUS "Next: set RAVEN_YOGA_VERSION in cmake/RavenYoga.cmake, record the release in ThirdParty/Yoga/README.md, and build")
