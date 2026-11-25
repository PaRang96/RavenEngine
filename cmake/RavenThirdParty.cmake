# Helpers shared by the vendored libraries' settings files (cmake/Raven<Lib>.cmake).
include_guard(GLOBAL)

# Third-party code builds without Raven's warning level.
function(raven_third_party target)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER ThirdParty)
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
