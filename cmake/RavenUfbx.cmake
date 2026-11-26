# ufbx 0.23.1: the FBX parser behind Private/Assets/FbxMesh.cpp (docs/AssetSystem.md).
#
# ThirdParty/ufbx is an unmodified upstream copy, replaced only by scripts/UpdateUfbx.cmake. Every Raven
# setting lives in this file; only Private/Assets/FbxMesh.cpp includes the library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_UFBX_VERSION 0.23.1)
set(RAVEN_UFBX_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/ufbx")

# Builds RavenFbx.
function(raven_add_ufbx)
    file(STRINGS "${RAVEN_UFBX_ROOT}/ufbx.h" line REGEX "^#define UFBX_HEADER_VERSION ufbx_pack_version\\(")
    if(NOT line MATCHES "ufbx_pack_version\\(([0-9]+), *([0-9]+), *([0-9]+)\\)")
        message(FATAL_ERROR "Cannot read UFBX_HEADER_VERSION from ThirdParty/ufbx/ufbx.h")
    endif()
    raven_check_version(ufbx ${RAVEN_UFBX_VERSION} "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    add_library(RavenFbx STATIC "${RAVEN_UFBX_ROOT}/ufbx.c")
    raven_third_party(RavenFbx)
    target_include_directories(RavenFbx SYSTEM PUBLIC "${RAVEN_UFBX_ROOT}")
endfunction()
