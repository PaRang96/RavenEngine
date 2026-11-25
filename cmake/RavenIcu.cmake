# ICU4C 79.1, its common library: bidi, script runs, line, word and grapheme breaks, normalization
# (decision U5; i18n joins with string tables).
#
# ThirdParty/icu4c is an unmodified upstream copy, replaced only by scripts/UpdateIcu.cmake. Every Raven setting
# lives in this file; only Private/Text/ and Private/UI/ include the library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_ICU_VERSION 79.1)
set(RAVEN_ICU_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/icu4c")

# Builds RavenIcu.
function(raven_add_icu)
    # ICU's common library (decision U5: i18n joins with string tables). Its data file, filtered to
    # what the text stack uses (cmake/RavenIcuData.txt), is loaded by Private/Text at startup; file
    # access, converters and services ICU would otherwise open itself are compiled out.
    file(STRINGS "${RAVEN_ICU_ROOT}/source/common/unicode/uvernum.h" line REGEX "^#define U_ICU_VERSION ")
    string(REGEX MATCH "[0-9]+\\.[0-9]+" version "${line}")
    raven_check_version(ICU4C ${RAVEN_ICU_VERSION} "${version}")
    file(GLOB icu_sources CONFIGURE_DEPENDS "${RAVEN_ICU_ROOT}/source/common/*.cpp")
    add_library(RavenIcu STATIC ${icu_sources} "${RAVEN_ICU_ROOT}/source/stubdata/stubdata.cpp")
    raven_third_party(RavenIcu)
    target_include_directories(RavenIcu SYSTEM PUBLIC "${RAVEN_ICU_ROOT}/source/common")
    target_include_directories(RavenIcu PRIVATE "${RAVEN_ICU_ROOT}/source/stubdata")
    target_compile_definitions(RavenIcu
        PUBLIC U_STATIC_IMPLEMENTATION U_USING_ICU_NAMESPACE=0 U_CHARSET_IS_UTF8=1 U_HIDE_OBSOLETE_UTF_OLD_H=1
            UCONFIG_NO_FILE_IO=1 UCONFIG_NO_CONVERSION=1 UCONFIG_NO_SERVICE=1 UCONFIG_NO_IDNA=1
            UCONFIG_NO_COLLATION=1 UCONFIG_NO_FORMATTING=1 UCONFIG_NO_TRANSLITERATION=1 UCONFIG_NO_REGULAR_EXPRESSIONS=1
        PRIVATE U_COMMON_IMPLEMENTATION U_ENABLE_DYLOAD=0)
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(RavenIcu PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    endif()
endfunction()
