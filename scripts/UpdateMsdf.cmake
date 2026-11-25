# Replaces ThirdParty/msdfgen and ThirdParty/msdf-atlas-gen with the files of their releases (their
# READMEs). The copies are never edited by hand; every Raven setting lives in cmake/RavenMsdf.cmake.
#
#   cmake -DMSDFGEN=<extracted msdfgen tag archive> -DATLAS=<extracted msdf-atlas-gen tag archive>
#         -P scripts/UpdateMsdf.cmake

cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED MSDFGEN OR NOT DEFINED ATLAS)
    message(FATAL_ERROR "Pass -DMSDFGEN=<msdfgen release> and -DATLAS=<msdf-atlas-gen release>")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_copy(msdfgen "${MSDFGEN}" core ext msdfgen.h msdfgen-ext.h LICENSE.txt README.md)
raven_vendor_copy(msdf-atlas-gen "${ATLAS}" msdf-atlas-gen LICENSE.txt README.md)
message(STATUS "Next: set RAVEN_MSDFGEN_VERSION and RAVEN_MSDF_ATLAS_GEN_VERSION in cmake/RavenMsdf.cmake and record both releases")
