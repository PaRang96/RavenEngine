# Replaces ThirdParty/msdfgen and ThirdParty/msdf-atlas-gen with the files of their releases (their
# READMEs). The copies are never edited by hand; every Raven setting lives in cmake/RavenMsdf.cmake.
#
#   cmake -DMSDFGEN=<extracted msdfgen tag archive> -DMSDFGEN_VERSION=<x.y.z>
#         -DATLAS=<extracted msdf-atlas-gen tag archive> -DATLAS_VERSION=<x.y> -P scripts/UpdateMsdf.cmake
#
# Neither library states its version in the kept files, so the versions given here are recorded in
# each copy's MANIFEST.txt, which cmake/RavenMsdf.cmake checks its pins against.

cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED MSDFGEN OR NOT DEFINED ATLAS OR NOT DEFINED MSDFGEN_VERSION OR NOT DEFINED ATLAS_VERSION)
    message(FATAL_ERROR "Pass -DMSDFGEN=<msdfgen release> -DMSDFGEN_VERSION=<version> -DATLAS=<msdf-atlas-gen release> "
        "-DATLAS_VERSION=<version>")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(msdfgen "${MSDFGEN}" VERSION ${MSDFGEN_VERSION} core ext msdfgen.h msdfgen-ext.h LICENSE.txt README.md)
raven_vendor_copy(msdf-atlas-gen "${ATLAS}" VERSION ${ATLAS_VERSION} msdf-atlas-gen LICENSE.txt README.md)
message(STATUS "Next: set RAVEN_MSDFGEN_VERSION and RAVEN_MSDF_ATLAS_GEN_VERSION in cmake/RavenMsdf.cmake and record both releases")
