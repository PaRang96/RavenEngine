# Tests enroll automatically by folder, like engine sources:
#   Tests/**/<Name>Tests.cpp            -> one test executable and one CTest entry, "<Name>Tests"
#   Tests/Gpu/**                        -> labeled "gpu" (needs a window and Vulkan device); others "cpu"
#   Tests/Benchmarks/<Name>Benchmark.cpp -> a benchmark executable, not registered with CTest
#   Tests/Support/**                    -> shared helpers compiled into every test and benchmark
# A test executable reports failure through a nonzero exit code (see Support/Check.hpp).
# Executables are placed beside RavenCore so they find its DLL and the staged Content/Shaders.

file(GLOB_RECURSE raven_test_sources CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*Tests.cpp")
file(GLOB_RECURSE raven_benchmark_sources CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/*Benchmark.cpp")
file(GLOB_RECURSE raven_test_support CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/Support/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Support/*.hpp")

function(raven_add_test_executable name source)
    add_executable(${name} "${source}" ${raven_test_support})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE RavenCore)
    raven_apply_warnings(${name})
    set_target_properties(${name} PROPERTIES
        FOLDER "Tests"
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:RavenCore>")
endfunction()

foreach(raven_test_source IN LISTS raven_test_sources)
    get_filename_component(raven_test_name "${raven_test_source}" NAME_WE)
    file(RELATIVE_PATH raven_test_relative "${CMAKE_CURRENT_SOURCE_DIR}" "${raven_test_source}")
    raven_add_test_executable(${raven_test_name} "${raven_test_source}")

    if(raven_test_relative MATCHES "^Gpu/")
        set(raven_test_label "gpu")
    else()
        set(raven_test_label "cpu")
    endif()

    add_test(NAME ${raven_test_name} COMMAND ${raven_test_name}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(${raven_test_name} PROPERTIES
        LABELS "${raven_test_label}"
        TIMEOUT 120)
endforeach()

foreach(raven_benchmark_source IN LISTS raven_benchmark_sources)
    get_filename_component(raven_benchmark_name "${raven_benchmark_source}" NAME_WE)
    raven_add_test_executable(${raven_benchmark_name} "${raven_benchmark_source}")
endforeach()

unset(raven_test_source)
unset(raven_test_name)
unset(raven_test_relative)
unset(raven_test_label)
unset(raven_benchmark_source)
unset(raven_benchmark_name)

# The determinism check (docs/GameServicesDesign.md, FD-48): the host plays the arena twice with the same
# seed and a scripted input lane and compares the simulation after every tick. It needs a window and a
# Vulkan device, so it is labeled "gpu" and runs locally.
if(TARGET RavenEngine AND TARGET ArenaGame)
    add_test(NAME DeterminismCheck
        COMMAND RavenEngine --game Arena --determinism-check 600
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:RavenEngine>")
    set_tests_properties(DeterminismCheck PROPERTIES LABELS gpu TIMEOUT 120)
endif()
