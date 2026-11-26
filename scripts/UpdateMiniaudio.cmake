# Replaces ThirdParty/miniaudio with the files of a miniaudio release (ThirdParty/miniaudio/README.md).
# The copy is never edited by hand; every Raven setting lives in cmake/RavenAudio.cmake, so an update
# is this script plus the version pin there.
#
#   cmake -DSOURCE=<folder holding miniaudio.h, miniaudio.c and LICENSE> -P scripts/UpdateMiniaudio.cmake

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<folder holding the miniaudio release files>")
endif()
file(REAL_PATH "${SOURCE}" source)

set(kept_files miniaudio.h miniaudio.c LICENSE)
foreach(path IN LISTS kept_files)
    if(NOT EXISTS "${source}/${path}")
        message(FATAL_ERROR "${source} is not a miniaudio release: ${path} is missing")
    endif()
endforeach()

set(version "")
foreach(part MAJOR MINOR REVISION)
    file(STRINGS "${source}/miniaudio.h" line REGEX "^#define MA_VERSION_${part} +[0-9]+")
    if(NOT line MATCHES "([0-9]+)$")
        message(FATAL_ERROR "Cannot read MA_VERSION_${part} from ${source}/miniaudio.h")
    endif()
    string(APPEND version "${CMAKE_MATCH_1}.")
endforeach()
string(REGEX REPLACE "\\.$" "" version "${version}")

include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(miniaudio "${source}" VERSION ${version} ${kept_files})
message(STATUS "Next: set RAVEN_MINIAUDIO_VERSION to ${version} in cmake/RavenAudio.cmake and record the release in ThirdParty/miniaudio/README.md")
