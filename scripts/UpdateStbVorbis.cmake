# Replaces ThirdParty/stb_vorbis with the files of an stb commit (ThirdParty/stb_vorbis/README.md).
# The copy is never edited by hand; every Raven setting lives in cmake/RavenVorbis.cmake, so an update
# is this script plus the version pin there.
#
#   cmake -DSOURCE=<folder holding stb_vorbis.c and LICENSE> -P scripts/UpdateStbVorbis.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<folder holding stb_vorbis.c and LICENSE>")
endif()
file(REAL_PATH "${SOURCE}" source)

set(kept_files stb_vorbis.c LICENSE)
foreach(path IN LISTS kept_files)
    if(NOT EXISTS "${source}/${path}")
        message(FATAL_ERROR "${source} is not an stb_vorbis download: ${path} is missing")
    endif()
endforeach()

file(STRINGS "${source}/stb_vorbis.c" line LIMIT_COUNT 1 REGEX "Ogg Vorbis audio decoder - v[0-9.]+")
if(NOT line MATCHES "v([0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "Cannot read the version from ${source}/stb_vorbis.c")
endif()
set(version "${CMAKE_MATCH_1}")

include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(stb_vorbis "${source}" VERSION ${version} ${kept_files})
message(STATUS "Next: set RAVEN_STB_VORBIS_VERSION to ${version} in cmake/RavenVorbis.cmake and record the commit in ThirdParty/stb_vorbis/README.md")
