# Module scanning is enabled here because tests live outside the src/aegis/ scope that sets it.
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# doctest entry point, compiled once and reused by every test executable.
add_library(aegis-test-main STATIC test_main.cpp)
target_link_libraries(aegis-test-main PUBLIC doctest::doctest)

# aegis_add_test(<module> SOURCES <a.cpp> ... LIBS <lib> ...)
# Builds one executable per module linking only that module's library, then registers its
# cases with CTest so `ctest` aggregates across every executable.
function(aegis_add_test module)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(aegis-tests-${module} ${T_SOURCES})
    target_link_libraries(aegis-tests-${module} PRIVATE aegis-test-main ${T_LIBS})
    doctest_discover_tests(aegis-tests-${module})
endfunction()

add_subdirectory(rhi)
add_subdirectory(math)
