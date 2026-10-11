# Module scanning is enabled here because tests live outside the src/aegis/ scope that sets it.
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# doctest entry point, compiled once and reused by every test executable.
add_library(aegis-test-main STATIC test_main.cpp)
target_link_libraries(aegis-test-main PUBLIC doctest::doctest)

# aegis_add_test(<module> SOURCES <a.cpp> ... LIBS <lib> ...)
# Builds one executable per module linking only that module's library, then registers its
# cases with CTest so `ctest` aggregates across every executable. Each case is labelled with its
# module, so `ctest -L <module>` runs one module (CTest names cases by TEST_CASE, not TEST_SUITE).
function(aegis_add_test module)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(aegis-tests-${module} ${T_SOURCES})
    target_link_libraries(aegis-tests-${module} PRIVATE aegis-test-main ${T_LIBS})
    doctest_discover_tests(aegis-tests-${module} PROPERTIES LABELS ${module})
endfunction()

add_subdirectory(rhi)
add_subdirectory(math)

# Device-level check: the aegis-test harness, headless for a fixed number of frames. It exits non-zero
# on any failed check or validation error. Needs a GPU with a Vulkan driver; `ctest -LE device` skips it.
add_test(NAME aegis-test-headless COMMAND aegis-test --headless --frames 600)
set_tests_properties(aegis-test-headless PROPERTIES LABELS device TIMEOUT 300)
