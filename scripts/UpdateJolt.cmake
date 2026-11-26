# Replaces ThirdParty/JoltPhysics with the library part of an extracted Jolt Physics release
# (ThirdParty/JoltPhysics/README.md). The copy is never edited by hand; every Raven setting lives in
# cmake/RavenJolt.cmake, so an update is this script plus the version pin there.
#
#   cmake -DSOURCE=<extracted JoltPhysics-x.y.z folder> -P scripts/UpdateJolt.cmake

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<extracted JoltPhysics release folder>")
endif()
file(REAL_PATH "${SOURCE}" source)

# The parts Raven builds from: the library, its build file and the license.
set(kept_folders Jolt)
set(kept_files Build/CMakeLists.txt LICENSE)
foreach(path IN LISTS kept_folders kept_files)
    if(NOT EXISTS "${source}/${path}")
        message(FATAL_ERROR "${source} is not a Jolt Physics release: ${path} is missing")
    endif()
endforeach()
if(NOT EXISTS "${source}/Jolt/Jolt.cmake")
    message(FATAL_ERROR "${source} is not a Jolt Physics release: Jolt/Jolt.cmake is missing")
endif()

file(STRINGS "${source}/Build/CMakeLists.txt" project_line REGEX "^project\\(JoltPhysics VERSION ")
if(NOT project_line MATCHES "VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "Cannot read the Jolt version from ${source}/Build/CMakeLists.txt")
endif()
set(version "${CMAKE_MATCH_1}")

include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(JoltPhysics "${source}" VERSION ${version} ${kept_folders} ${kept_files})
message(STATUS "Next: set RAVEN_JOLT_VERSION to ${version} in cmake/RavenJolt.cmake and record the release in ThirdParty/JoltPhysics/README.md")
