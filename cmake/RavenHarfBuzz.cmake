# HarfBuzz 15.2.0: text shaping for the text stack (decision U2).
#
# ThirdParty/HarfBuzz is an unmodified upstream copy, replaced only by scripts/UpdateHarfBuzz.cmake. Every Raven setting
# lives in this file; only Private/Text/ and Private/UI/ include the library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_HARFBUZZ_VERSION 15.2.0)
set(RAVEN_HARFBUZZ_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/HarfBuzz")

# Builds RavenHarfBuzz.
function(raven_add_harfbuzz)
    # HarfBuzz: its single-file build. Fonts are read through HarfBuzz's own OpenType functions, so it
    # needs no FreeType integration; it stays thread-safe for shaping on workers.
    file(STRINGS "${RAVEN_HARFBUZZ_ROOT}/src/hb-version.h" line REGEX "^#define HB_VERSION_STRING ")
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" version "${line}")
    raven_check_version(HarfBuzz ${RAVEN_HARFBUZZ_VERSION} "${version}")
    add_library(RavenHarfBuzz STATIC "${RAVEN_HARFBUZZ_ROOT}/src/harfbuzz.cc")
    raven_third_party(RavenHarfBuzz)
    # The single-file build has more sections than an object file holds by default.
    if(MSVC)
        target_compile_options(RavenHarfBuzz PRIVATE /bigobj)
    endif()
    target_include_directories(RavenHarfBuzz SYSTEM PUBLIC "${RAVEN_HARFBUZZ_ROOT}/src")
    target_compile_definitions(RavenHarfBuzz PUBLIC HB_NO_DEPRECATED)
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(RavenHarfBuzz PRIVATE Threads::Threads m)
    endif()
endfunction()
