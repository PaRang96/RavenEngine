# Capture only build-tool variables from the successful host configuration.
# Runtime game builds can then run without inheriting a developer shell.
function(raven_capture_msvc_environment output)
    foreach(name IN ITEMS INCLUDE LIB)
        if("$ENV{${name}}" STREQUAL "")
            message(FATAL_ERROR
                "Missing MSVC ${name} environment. Configure through CMake Tools or a Visual Studio developer shell.")
        endif()
    endforeach()

    set(content "# Generated MSVC build environment. Reconfigure the host to refresh.\n")
    foreach(name IN ITEMS PATH INCLUDE LIB LIBPATH VSINSTALLDIR VCINSTALLDIR
        VCToolsInstallDir WindowsSdkDir WindowsSDKVersion UniversalCRTSdkDir UCRTVersion)
        set(value "$ENV{${name}}")
        # Bracket arguments preserve spaces, semicolons, backslashes and dollar
        # characters literally. Choose a delimiter absent from the value.
        set(equals "=")
        string(FIND "${value}" "]${equals}]" closing)
        while(NOT closing EQUAL -1)
            string(APPEND equals "=")
            string(FIND "${value}" "]${equals}]" closing)
        endwhile()
        string(APPEND content "set(ENV{${name}} [${equals}[${value}]${equals}])\n")
    endforeach()

    set(previous "")
    if(EXISTS "${output}")
        file(READ "${output}" previous)
    endif()
    if(NOT previous STREQUAL content)
        get_filename_component(directory "${output}" DIRECTORY)
        file(MAKE_DIRECTORY "${directory}")
        file(WRITE "${output}" "${content}")
    endif()
endfunction()
