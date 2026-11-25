# FreeType 2.15.0: font loading for the text stack (decision U2).
#
# ThirdParty/FreeType is an unmodified upstream copy, replaced only by scripts/UpdateFreeType.cmake. Every Raven setting
# lives in this file; only Private/Text/ and Private/UI/ include the library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_FREETYPE_VERSION 2.15.0)
set(RAVEN_FREETYPE_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/FreeType")

# Builds RavenFreeType.
function(raven_add_freetype)
    # FreeType: its own module list (include/freetype/config/ftmodule.h) and options (ftoption.h),
    # unmodified. Its zlib copy serves compressed fonts; no system libraries are used.
    file(STRINGS "${RAVEN_FREETYPE_ROOT}/include/freetype/freetype.h" lines REGEX "^#define FREETYPE_(MAJOR|MINOR|PATCH) ")
    string(REGEX REPLACE ".*MAJOR +([0-9]+).*MINOR +([0-9]+).*PATCH +([0-9]+).*" "\\1.\\2.\\3" version "${lines}")
    raven_check_version(FreeType ${RAVEN_FREETYPE_VERSION} "${version}")
    set(freetype_sources
        autofit/autofit.c base/ftbase.c base/ftbbox.c base/ftbdf.c base/ftbitmap.c base/ftcid.c base/ftfstype.c
        base/ftgasp.c base/ftglyph.c base/ftgxval.c base/ftinit.c base/ftmm.c base/ftotval.c base/ftpatent.c
        base/ftpfr.c base/ftstroke.c base/ftsynth.c base/fttype1.c base/ftwinfnt.c bdf/bdf.c bzip2/ftbzip2.c
        cache/ftcache.c cff/cff.c cid/type1cid.c gzip/ftgzip.c hvf/hvf.c lzw/ftlzw.c pcf/pcf.c pfr/pfr.c
        psaux/psaux.c pshinter/pshinter.c psnames/psnames.c raster/raster.c sdf/sdf.c sfnt/sfnt.c
        smooth/smooth.c svg/svg.c truetype/truetype.c type1/type1.c type42/type42.c winfonts/winfnt.c)
    list(TRANSFORM freetype_sources PREPEND "${RAVEN_FREETYPE_ROOT}/src/")
    if(WIN32)
        list(APPEND freetype_sources "${RAVEN_FREETYPE_ROOT}/builds/windows/ftsystem.c"
            "${RAVEN_FREETYPE_ROOT}/builds/windows/ftdebug.c")
    else()
        list(APPEND freetype_sources "${RAVEN_FREETYPE_ROOT}/src/base/ftsystem.c"
            "${RAVEN_FREETYPE_ROOT}/src/base/ftdebug.c")
    endif()
    add_library(RavenFreeType STATIC ${freetype_sources})
    raven_third_party(RavenFreeType)
    target_include_directories(RavenFreeType SYSTEM PUBLIC "${RAVEN_FREETYPE_ROOT}/include")
    target_compile_definitions(RavenFreeType PRIVATE FT2_BUILD_LIBRARY)
    if(MSVC)
        target_compile_definitions(RavenFreeType PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()
