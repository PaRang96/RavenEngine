# Capture only build-tool variables from the successful host configuration.
# Runtime game builds can then run without inheriting a developer shell.
function(raven_capture_build_environment output)
    if(MSVC)
        foreach(name IN ITEMS INCLUDE LIB)
            if("$ENV{${name}}" STREQUAL "")
                message(FATAL_ERROR
                    "Missing MSVC ${name} environment. Configure through CMake Tools or a Visual Studio developer shell.")
            endif()
        endforeach()
        set(names PATH INCLUDE LIB LIBPATH VSINSTALLDIR VCINSTALLDIR
            VCToolsInstallDir WindowsSdkDir WindowsSDKVersion UniversalCRTSdkDir UCRTVersion)
        set(content "# Generated MSVC build environment. Reconfigure the host to refresh.\n")
    else()
        # GCC and Clang need no developer shell: the compilers and the tools on PATH are enough.
        set(names CC CXX PATH)
        set(content "# Generated build environment. Reconfigure the host to refresh.\n")
    endif()

    foreach(name IN LISTS names)
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

# A C++ UTF-8 string literal (u8"...") holding `value`, with every byte escaped, for generated
# headers: the compiler's source character set (the system code page without /utf-8) cannot change
# a path that holds non-ASCII characters.
function(raven_utf8_literal value out)
    string(HEX "${value}" hex)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\\\x\\1" escaped "${hex}")
    set(${out} "u8\"${escaped}\"" PARENT_SCOPE)
endfunction()
