# Yoga 3.2.1: flex layout for the UI stack (decision U3).
#
# ThirdParty/Yoga is an unmodified upstream copy, replaced only by scripts/UpdateYoga.cmake. Every Raven setting
# lives in this file; only Private/Text/ and Private/UI/ include the library.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/RavenThirdParty.cmake")

set(RAVEN_YOGA_VERSION 3.2.1)
set(RAVEN_YOGA_ROOT "${PROJECT_SOURCE_DIR}/ThirdParty/Yoga")

# Builds RavenYoga.
function(raven_add_yoga)
    # Yoga: every source of its library folder.
    file(GLOB_RECURSE yoga_sources CONFIGURE_DEPENDS "${RAVEN_YOGA_ROOT}/yoga/*.cpp")
    add_library(RavenYoga STATIC ${yoga_sources})
    raven_third_party(RavenYoga)
    target_include_directories(RavenYoga SYSTEM PUBLIC "${RAVEN_YOGA_ROOT}")
    target_compile_features(RavenYoga PUBLIC cxx_std_20)
endfunction()
