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

add_subdirectory(assets)
add_subdirectory(base)
add_subdirectory(math)
add_subdirectory(platform)
add_subdirectory(reflect)
add_subdirectory(render_graph)
add_subdirectory(renderer)
add_subdirectory(rhi)
add_subdirectory(world)

# aegis_add_device_test(<name> ARGS <aegis-test args> ... [LABELS <label> ...])
# Registers an aegis-test run that needs a GPU with a Vulkan driver (`ctest -LE device` skips it). It
# exits non-zero on any failed check or validation error. aegis-test returns 77 when the device lacks a
# feature the run needs, which CTest reports as skipped. Extra LABELS name that feature (`mesh-shaders`).
# The `gpu` resource lock keeps device runs from overlapping when ctest runs tests in parallel.
set(AEGIS_TEST_SKIP_RETURN_CODE 77) # Keep in step with src/aegis/test/test.cppm
function(aegis_add_device_test name)
    cmake_parse_arguments(T "" "" "ARGS;LABELS" ${ARGN})
    add_test(NAME ${name} COMMAND aegis-test ${T_ARGS})
    set_tests_properties(${name} PROPERTIES
        LABELS "device;${T_LABELS}"
        RESOURCE_LOCK gpu
        TIMEOUT 300
        SKIP_RETURN_CODE ${AEGIS_TEST_SKIP_RETURN_CODE})
endfunction()

# The device tests load compiled shaders, so they exist only when the shaders are built
# (-DCOMPILE_SHADERS=OFF registers none).
if(TARGET compile_shaders)
    # The harness headless for a fixed number of frames.
    aegis_add_device_test(aegis-test-headless ARGS --headless --frames 600)

    # The same harness rendering a loaded scene through the render graph (aegis.render_graph).
    aegis_add_device_test(aegis-test-scene ARGS --headless --scene --frames 120)
    aegis_add_device_test(aegis-test-scene-gpu-driven ARGS --headless --gpu-driven --frames 120 LABELS mesh-shaders)

    # The scene changes while frames are in flight: a model loaded, one destroyed, materials edited.
    aegis_add_device_test(aegis-test-scene-mutation ARGS --headless --scene --mutate-scene --frames 150)
    aegis_add_device_test(aegis-test-scene-gpu-driven-mutation ARGS --headless --gpu-driven --mutate-scene --frames 150 LABELS mesh-shaders)

    # Reference images: the last frame against tests/golden, generated on the dev GPU (see tests/README.md). Local
    # only: the lavapipe CI job runs the other device tests but excludes these with -LE golden, since software
    # rendering differs from the dev GPU (the triangle by 1 per channel, the scenes by up to 176 on ~80k pixels:
    # beyond any tolerance that would still catch a regression). The stats window is left out, its timings
    # change every run.
    aegis_add_device_test(aegis-test-golden-triangle
        ARGS --headless --frames 120 --compare ${CMAKE_CURRENT_SOURCE_DIR}/golden/triangle.png --tolerance 0
        LABELS golden)
    aegis_add_device_test(aegis-test-golden-scene
        ARGS --headless --scene --frames 120 --no-overlay --compare ${CMAKE_CURRENT_SOURCE_DIR}/golden/scene.png --tolerance 0
        LABELS golden)
    aegis_add_device_test(aegis-test-golden-scene-gpu-driven
        ARGS --headless --gpu-driven --frames 120 --no-overlay --compare ${CMAKE_CURRENT_SOURCE_DIR}/golden/scene-gpu-driven.png --tolerance 0
        LABELS golden mesh-shaders)
endif()
