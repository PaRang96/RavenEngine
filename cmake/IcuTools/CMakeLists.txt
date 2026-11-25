# Builds ICU's own icupkg tool from a full ICU source release, for scripts/UpdateIcu.cmake only. It filters
# ICU's prebuilt data package down to the items in cmake/RavenIcuData.txt. Not part of the engine build.
#   cmake -S cmake/IcuTools -B <build> -DICU_SOURCE=<folder holding icu/source>
cmake_minimum_required(VERSION 3.24)
project(RavenIcuTools CXX)

if(NOT DEFINED ICU_SOURCE)
    message(FATAL_ERROR "Pass -DICU_SOURCE=<the icu folder of an ICU4C source release>")
endif()
file(TO_CMAKE_PATH "${ICU_SOURCE}" ICU_SOURCE)
set(CMAKE_CXX_STANDARD 17)
set(source "${ICU_SOURCE}/source")
file(GLOB common "${source}/common/*.cpp")
file(GLOB i18n "${source}/i18n/*.cpp")
file(GLOB toolutil "${source}/tools/toolutil/*.cpp")
# toolutil's swapping and debugging helpers reach into i18n, so the tool links all three libraries.
add_executable(icupkg "${source}/tools/icupkg/icupkg.cpp" ${common} ${i18n} ${toolutil} "${source}/stubdata/stubdata.cpp")
target_include_directories(icupkg PRIVATE "${source}/common" "${source}/i18n" "${source}/tools/toolutil" "${source}/stubdata")
target_compile_definitions(icupkg PRIVATE U_STATIC_IMPLEMENTATION U_COMMON_IMPLEMENTATION U_TOOLUTIL_IMPLEMENTATION
    U_I18N_IMPLEMENTATION)
if(MSVC)
    target_compile_options(icupkg PRIVATE /w /utf-8)
else()
    target_compile_options(icupkg PRIVATE -w)
    find_package(Threads REQUIRED)
    target_link_libraries(icupkg PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
