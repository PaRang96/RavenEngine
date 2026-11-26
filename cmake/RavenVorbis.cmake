# Ogg Vorbis decoding through stb_vorbis (docs/GameServicesDesign.md, C8, decision A6; owner, 2026-11-25).
#
# Only Private/Audio/AudioClip.cpp calls stb_vorbis, behind DecodeAudio. ThirdParty/stb_vorbis is an
# unmodified copy of an upstream file, replaced only by scripts/UpdateStbVorbis.cmake. Every Raven
# setting lives in this file.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_STB_VORBIS_VERSION 1.25)
set(RAVEN_STB_VORBIS_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/stb_vorbis")

function(raven_add_vorbis target)
    file(STRINGS "${RAVEN_STB_VORBIS_ROOT}/stb_vorbis.c" line LIMIT_COUNT 1 REGEX "Ogg Vorbis audio decoder - v[0-9.]+")
    if(NOT line MATCHES "v([0-9]+\\.[0-9]+)")
        message(FATAL_ERROR "Cannot read the version from ThirdParty/stb_vorbis/stb_vorbis.c")
    endif()
    if(NOT CMAKE_MATCH_1 VERSION_EQUAL RAVEN_STB_VORBIS_VERSION)
        message(FATAL_ERROR "ThirdParty/stb_vorbis holds stb_vorbis ${CMAKE_MATCH_1}, not ${RAVEN_STB_VORBIS_VERSION}. "
            "Update it with scripts/UpdateStbVorbis.cmake and keep RAVEN_STB_VORBIS_VERSION in step (ThirdParty/stb_vorbis/README.md).")
    endif()

    add_library(RavenStbVorbis STATIC "${RAVEN_STB_VORBIS_ROOT}/stb_vorbis.c")
    set_source_files_properties("${RAVEN_STB_VORBIS_ROOT}/stb_vorbis.c" PROPERTIES LANGUAGE C)
    raven_third_party(RavenStbVorbis)
    target_include_directories(RavenStbVorbis SYSTEM PUBLIC "${RAVEN_STB_VORBIS_ROOT}")
    # PUBLIC: the declarations RavenCore sees must match the compiled implementation. Raven reads files
    # itself, so the FILE* functions are compiled out.
    target_compile_definitions(RavenStbVorbis PUBLIC STB_VORBIS_NO_STDIO)
    if(UNIX)
        target_link_libraries(RavenStbVorbis PRIVATE m)
    endif()

    target_link_libraries(${target} PRIVATE RavenStbVorbis)
endfunction()
