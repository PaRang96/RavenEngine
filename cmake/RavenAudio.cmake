# The audio device backend and miniaudio (docs/UIAudioLibraries.md, decision A1, 2026-11-25).
#
# The engine reaches the device only through Private/Audio/AudioDevice.hpp. RAVEN_AUDIO_BACKEND picks
# the one backend file under Private/Audio/Backends/ that implements it; nothing else in the engine
# names the library. miniaudio also serves the clip conversion (Private/Audio/AudioClip.cpp), so it is
# built for every backend: a backend other than miniaudio compiles its device code out
# (MA_NO_DEVICE_IO, option C in the note).
#
# ThirdParty/miniaudio is an unmodified copy of an upstream release, replaced only by
# scripts/UpdateMiniaudio.cmake. Every Raven setting lives in this file.
include_guard(GLOBAL)

set(RAVEN_MINIAUDIO_VERSION 0.11.26)
set(RAVEN_MINIAUDIO_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/miniaudio")
set(RAVEN_AUDIO_BACKEND "miniaudio" CACHE STRING "Audio device backend (Private/Audio/Backends)")
set_property(CACHE RAVEN_AUDIO_BACKEND PROPERTY STRINGS miniaudio)

function(raven_add_audio target)
    set(version "")
    foreach(part MAJOR MINOR REVISION)
        file(STRINGS "${RAVEN_MINIAUDIO_ROOT}/miniaudio.h" line REGEX "^#define MA_VERSION_${part} +[0-9]+")
        if(NOT line MATCHES "([0-9]+)$")
            message(FATAL_ERROR "Cannot read MA_VERSION_${part} from ThirdParty/miniaudio/miniaudio.h")
        endif()
        string(APPEND version "${CMAKE_MATCH_1}.")
    endforeach()
    string(REGEX REPLACE "\\.$" "" version "${version}")
    if(NOT version VERSION_EQUAL RAVEN_MINIAUDIO_VERSION)
        message(FATAL_ERROR "ThirdParty/miniaudio holds miniaudio ${version}, not ${RAVEN_MINIAUDIO_VERSION}. "
            "Update it with scripts/UpdateMiniaudio.cmake and keep RAVEN_MINIAUDIO_VERSION in step (ThirdParty/miniaudio/README.md).")
    endif()

    set(backends miniaudio)
    if(NOT RAVEN_AUDIO_BACKEND IN_LIST backends)
        message(FATAL_ERROR "RAVEN_AUDIO_BACKEND must be one of: ${backends}")
    endif()

    add_library(RavenMiniaudio STATIC "${RAVEN_MINIAUDIO_ROOT}/miniaudio.c")
    set_target_properties(RavenMiniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER ThirdParty)
    target_include_directories(RavenMiniaudio SYSTEM PUBLIC "${RAVEN_MINIAUDIO_ROOT}")
    # PUBLIC: the declarations RavenCore sees must match the compiled implementation.
    target_compile_definitions(RavenMiniaudio PUBLIC
        # Raven owns the mixer. Its WAV, FLAC and MP3 decoders serve clip loading (decision A6).
        MA_NO_ENGINE MA_NO_NODE_GRAPH MA_NO_RESOURCE_MANAGER
        MA_NO_ENCODING MA_NO_GENERATION
        MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_WASAPI MA_ENABLE_COREAUDIO
        MA_ENABLE_PULSEAUDIO MA_ENABLE_ALSA)
    if(NOT RAVEN_AUDIO_BACKEND STREQUAL "miniaudio")
        target_compile_definitions(RavenMiniaudio PUBLIC MA_NO_DEVICE_IO)
    endif()
    if(APPLE)
        # Runtime-linked frameworks fail notarization; link them instead.
        target_compile_definitions(RavenMiniaudio PUBLIC MA_NO_RUNTIME_LINKING)
        find_library(RAVEN_COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
        find_library(RAVEN_COREAUDIO_LIBRARY CoreAudio REQUIRED)
        find_library(RAVEN_AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
        target_link_libraries(RavenMiniaudio PUBLIC
            ${RAVEN_COREFOUNDATION_LIBRARY} ${RAVEN_COREAUDIO_LIBRARY} ${RAVEN_AUDIOTOOLBOX_LIBRARY})
    elseif(UNIX)
        # PulseAudio and ALSA are opened at runtime: no development packages are needed.
        find_package(Threads REQUIRED)
        target_link_libraries(RavenMiniaudio PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)
    endif()

    target_link_libraries(${target} PRIVATE RavenMiniaudio)
    if(RAVEN_AUDIO_BACKEND STREQUAL "miniaudio")
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/RavenEngine/Private/Audio/Backends/MiniaudioDevice.cpp")
    endif()
endfunction()
