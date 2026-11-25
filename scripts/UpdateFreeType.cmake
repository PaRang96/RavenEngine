# Replaces ThirdParty/FreeType with the files of a FreeType release (ThirdParty/FreeType/README.md). The copy is
# never edited by hand; every Raven setting lives in its cmake/Raven<Lib>.cmake, so an update is this
# script plus the version pin there.
#
#   cmake -DSOURCE=<the extracted freetype-<version>.tar.xz from download.savannah.gnu.org/releases/freetype> -P scripts/UpdateFreeType.cmake

cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<the extracted FreeType release>")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(FreeType "${SOURCE}" include src builds/windows LICENSE.TXT README docs/FTL.TXT docs/GPLv2.TXT)
message(STATUS "Next: set RAVEN_FREETYPE_VERSION in cmake/RavenFreeType.cmake, record the release in ThirdParty/FreeType/README.md, and build")
