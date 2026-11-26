# Replaces ThirdParty/icu4c with ICU4C's common library and a filtered data package from an ICU source
# release (ThirdParty/icu4c/README.md). The sources are copied unmodified. The data package is ICU's own
# prebuilt one (source/data/in/icudt<N>l.dat), reduced to the items in cmake/RavenIcuData.txt by ICU's
# own icupkg tool, which this script builds from the same release (cmake/IcuTools).
#
#   cmake -DSOURCE=<the icu folder of icu4c-<version>-sources.tgz> -P scripts/UpdateIcu.cmake
#
# Run it from a developer environment where CMake finds a C++ compiler.

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "Pass -DSOURCE=<the icu folder of an ICU4C source release>")
endif()
file(REAL_PATH "${SOURCE}" source)
foreach(path source/common source/stubdata source/tools/icupkg LICENSE)
    if(NOT EXISTS "${source}/${path}")
        message(FATAL_ERROR "${source} is not an ICU4C source release: ${path} is missing")
    endif()
endforeach()
file(STRINGS "${source}/source/common/unicode/uvernum.h" line REGEX "^#define U_ICU_VERSION_SHORT ")
string(REGEX MATCH "\"([0-9]+)\"" match "${line}")
set(major "${CMAKE_MATCH_1}")
file(STRINGS "${source}/source/common/unicode/uvernum.h" line REGEX "^#define U_ICU_VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+" version "${line}")
set(package "${source}/source/data/in/icudt${major}l.dat")
if(NOT EXISTS "${package}")
    message(FATAL_ERROR "${package} is missing; use the sources archive, which carries the prebuilt data")
endif()

get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(target "${repository}/ThirdParty/icu4c")
set(work "${repository}/out/IcuUpdate")
file(REMOVE_RECURSE "${work}")

# 1. ICU's icupkg, built from this release.
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${repository}/cmake/IcuTools" -B "${work}/tools"
        -DCMAKE_BUILD_TYPE=Release "-DICU_SOURCE=${source}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Configuring cmake/IcuTools failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work}/tools" --config Release RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Building icupkg failed")
endif()
file(GLOB_RECURSE icupkg "${work}/tools/icupkg" "${work}/tools/icupkg.exe")
list(GET icupkg 0 icupkg)

# 2. The items to keep: every item of the package that matches a glob in cmake/RavenIcuData.txt.
execute_process(COMMAND "${icupkg}" -l "${package}" OUTPUT_VARIABLE items RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "icupkg could not list ${package}")
endif()
string(REPLACE "\r" "" items "${items}")
string(REPLACE "\n" ";" items "${items}")
file(STRINGS "${repository}/cmake/RavenIcuData.txt" globs REGEX "^[^#]")
set(kept "")
foreach(item IN LISTS items)
    foreach(glob IN LISTS globs)
        string(REPLACE "." "\\." pattern "${glob}")
        string(REPLACE "*" ".*" pattern "${pattern}")
        if(item MATCHES "^${pattern}$")
            list(APPEND kept "${item}")
            break()
        endif()
    endforeach()
endforeach()
list(LENGTH kept count)
if(count EQUAL 0)
    message(FATAL_ERROR "No ICU data item matched cmake/RavenIcuData.txt")
endif()
list(JOIN kept "\n" listing)
file(WRITE "${work}/items.txt" "${listing}\n")

# 3. Extract those items and pack them into a new package of the same name.
file(MAKE_DIRECTORY "${work}/items")
execute_process(COMMAND "${icupkg}" -x "${work}/items.txt" -d "${work}/items" "${package}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "icupkg could not extract the kept items")
endif()
execute_process(COMMAND "${icupkg}" -tl -a "${work}/items.txt" -s "${work}/items" new "${work}/icudt${major}l.dat"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "icupkg could not write the filtered package")
endif()

# 4. Replace everything except Raven's own README.md.
file(GLOB children LIST_DIRECTORIES true "${target}/*")
foreach(child IN LISTS children)
    get_filename_component(name "${child}" NAME)
    if(NOT name STREQUAL "README.md")
        file(REMOVE_RECURSE "${child}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${target}/source" "${target}/data")
file(COPY "${source}/source/common" "${source}/source/stubdata" DESTINATION "${target}/source")
file(COPY "${source}/LICENSE" DESTINATION "${target}")
file(COPY "${work}/icudt${major}l.dat" DESTINATION "${target}/data")
file(REMOVE_RECURSE "${work}")

message(STATUS "Copied ICU4C ${version} into ${target}, with ${count} data items")
include("${CMAKE_CURRENT_LIST_DIR}/VendorCopy.cmake")
raven_vendor_manifest(icu4c VERSION ${version})
message(STATUS "Next: set RAVEN_ICU_VERSION to ${version} in cmake/RavenIcu.cmake, update the data file name in "
    "Private/Text if the major version changed, and record the release in ThirdParty/icu4c/README.md")
