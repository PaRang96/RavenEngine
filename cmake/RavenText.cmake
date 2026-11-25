# The text and layout libraries of the UI stack (docs/UIAudioLibraries.md, decisions U2, U3 and U5;
# docs/UIDesign.md). Each has its own settings file and update script, per the vendoring rule:
#   FreeType 2.15.0          cmake/RavenFreeType.cmake   scripts/UpdateFreeType.cmake
#   HarfBuzz 15.2.0          cmake/RavenHarfBuzz.cmake   scripts/UpdateHarfBuzz.cmake
#   msdfgen 1.13.1,
#   msdf-atlas-gen 1.4       cmake/RavenMsdf.cmake       scripts/UpdateMsdf.cmake
#   Yoga 3.2.1               cmake/RavenYoga.cmake       scripts/UpdateYoga.cmake
#   ICU4C 79.1 (common)      cmake/RavenIcu.cmake        scripts/UpdateIcu.cmake
include_guard(GLOBAL)

foreach(library FreeType HarfBuzz Msdf Yoga Icu)
    include("${CMAKE_CURRENT_LIST_DIR}/Raven${library}.cmake")
endforeach()

# Glyph generation in an unoptimized build is slow enough that new text appears seconds late; turn off
# to step through the libraries' own code in a debugger.
option(RAVEN_TEXT_OPTIMIZE_DEBUG "Compile the text and layout libraries with optimization in Debug builds" ON)

function(raven_add_text target)
    # Local to this function, so they reach the libraries' directory only, as RAVEN_JOLT_OPTIMIZE_DEBUG does.
    if(RAVEN_TEXT_OPTIMIZE_DEBUG AND MSVC)
        set(CMAKE_C_FLAGS_DEBUG "/O2 /Ob2")
        set(CMAKE_CXX_FLAGS_DEBUG "/O2 /Ob2")
    elseif(RAVEN_TEXT_OPTIMIZE_DEBUG)
        set(CMAKE_C_FLAGS_DEBUG "-O2")
        set(CMAKE_CXX_FLAGS_DEBUG "-O2")
    endif()
    add_subdirectory("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RavenTextLibraries" "${CMAKE_BINARY_DIR}/ThirdParty/Text")
    target_link_libraries(${target} PRIVATE RavenFreeType RavenHarfBuzz RavenMsdfgen RavenMsdfAtlasGen RavenYoga RavenIcu)
endfunction()
