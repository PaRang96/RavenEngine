# msdfgen 1.13.1 and msdf-atlas-gen 1.4 (one author, released together): distance-field glyphs,
# glyph geometry and rectangle packing for the text stack (decision U2).
#
# ThirdParty/msdfgen and ThirdParty/msdf-atlas-gen is an unmodified upstream copy, replaced only by scripts/UpdateMsdf.cmake. Every Raven setting
# lives in this file; only Private/Text/ and Private/UI/ include the library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")
include("${PROJECT_SOURCE_DIR}/scripts/VendorCopy.cmake")

set(RAVEN_MSDFGEN_VERSION 1.13.1)
set(RAVEN_MSDF_ATLAS_GEN_VERSION 1.4)
set(RAVEN_MSDFGEN_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/msdfgen")
set(RAVEN_MSDF_ATLAS_GEN_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/msdf-atlas-gen")

# Builds RavenMsdfgen and RavenMsdfAtlasGen.
function(raven_add_msdf)
    # Neither copy states its version, so the pins are checked against the versions their manifests
    # recorded (scripts/UpdateMsdf.cmake).
    raven_vendor_manifest_version(msdfgen msdfgen_version)
    raven_check_version(msdfgen ${RAVEN_MSDFGEN_VERSION} "${msdfgen_version}")
    raven_vendor_manifest_version(msdf-atlas-gen atlas_version)
    raven_check_version(msdf-atlas-gen ${RAVEN_MSDF_ATLAS_GEN_VERSION} "${atlas_version}")
    # msdfgen: the core and its FreeType font import. Its PNG, SVG and Skia extras stay out, and its
    # generated config header is replaced by the same macros given here.
    set(msdfgen_definitions MSDFGEN_PUBLIC= MSDFGEN_EXT_PUBLIC= MSDFGEN_USE_CPP11
        MSDFGEN_DISABLE_PNG MSDFGEN_DISABLE_SVG MSDFGEN_VERSION=${RAVEN_MSDFGEN_VERSION})
    file(GLOB msdfgen_sources CONFIGURE_DEPENDS "${RAVEN_MSDFGEN_ROOT}/core/*.cpp")
    add_library(RavenMsdfgen STATIC ${msdfgen_sources} "${RAVEN_MSDFGEN_ROOT}/ext/import-font.cpp")
    raven_third_party(RavenMsdfgen)
    target_include_directories(RavenMsdfgen SYSTEM PUBLIC "${RAVEN_MSDFGEN_ROOT}")
    target_compile_definitions(RavenMsdfgen PUBLIC ${msdfgen_definitions})
    target_link_libraries(RavenMsdfgen PUBLIC RavenFreeType)

    # msdf-atlas-gen: glyph geometry, glyph generators and rectangle packing; its command-line tool,
    # image export and Artery font export stay out.
    set(atlas_sources GlyphGeometry.cpp glyph-generators.cpp RectanglePacker.cpp Padding.cpp Workload.cpp)
    list(TRANSFORM atlas_sources PREPEND "${RAVEN_MSDF_ATLAS_GEN_ROOT}/msdf-atlas-gen/")
    add_library(RavenMsdfAtlasGen STATIC ${atlas_sources})
    raven_third_party(RavenMsdfAtlasGen)
    target_include_directories(RavenMsdfAtlasGen SYSTEM PUBLIC "${RAVEN_MSDF_ATLAS_GEN_ROOT}")
    target_compile_definitions(RavenMsdfAtlasGen PUBLIC MSDF_ATLAS_NO_ARTERY_FONT MSDF_ATLAS_PUBLIC=)
    target_link_libraries(RavenMsdfAtlasGen PUBLIC RavenMsdfgen)
endfunction()
