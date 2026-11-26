# Helpers shared by the vendored libraries' settings files (cmake/Raven<Lib>.cmake).
include_guard(GLOBAL)

# Third-party code builds without Raven's warning level.
function(raven_third_party target)
    # Plain program-database debug information: Edit and Continue (/ZI, the engine's Debug default under
    # policy CMP0141) does not apply to libraries that are never edited, and conflicts with the optimized
    # Debug flags of RAVEN_TEXT_OPTIMIZE_DEBUG.
    # Hidden visibility: a library linked into RavenCore never exports its symbols, so it cannot clash
    # with a system copy another library loads (AA2-PLAT-04). No effect on Windows.
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER ThirdParty
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>"
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    # Sources are UTF-8; MSVC would otherwise read them in the system code page (Korean on the laptop).
    if(MSVC)
        target_compile_options(${target} PRIVATE /w /utf-8)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

function(raven_check_version library expected actual)
    if(NOT "${actual}" VERSION_EQUAL "${expected}")
        message(FATAL_ERROR "ThirdParty holds ${library} ${actual}, not ${expected}. Update it with its script "
            "under scripts/ and keep the version in its cmake/Raven<Lib>.cmake in step (its ThirdParty README).")
    endif()
endfunction()
