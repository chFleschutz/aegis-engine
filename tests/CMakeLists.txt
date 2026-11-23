# Module scanning is enabled here because tests live outside the aegis/ scope that sets it.
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)

# doctest entry point, compiled once and reused by every test executable.
add_library(aegis-test-main STATIC test_main.cpp)
target_link_libraries(aegis-test-main PUBLIC doctest::doctest)

# aegis_add_test(<module> SOURCES <a.cpp> ... LIBS <lib> ...)
# Builds one executable per module linking only that module's library, then registers its
# cases with CTest so `ctest` aggregates across every executable. Each case is labelled with its
# module, so `ctest -L <module>` runs one module (CTest names cases by TEST_CASE, not TEST_SUITE).
# A case that runs past AEGIS_UNIT_TEST_TIMEOUT seconds fails: every commit runs the suite, so it stays fast.
set(AEGIS_UNIT_TEST_TIMEOUT 30)
function(aegis_add_test module)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(aegis-tests-${module} ${T_SOURCES})
    target_link_libraries(aegis-tests-${module} PRIVATE aegis-test-main ${T_LIBS})
    doctest_discover_tests(
        aegis-tests-${module}
        PROPERTIES LABELS ${module} TIMEOUT ${AEGIS_UNIT_TEST_TIMEOUT}
    )
endfunction()

add_subdirectory(asset_library)
add_subdirectory(assets)
add_subdirectory(audio)
add_subdirectory(core)
add_subdirectory(device) # aegis-test, the harness that checks a real device
add_subdirectory(editor)
add_subdirectory(engine)
add_subdirectory(math)
add_subdirectory(paths)
add_subdirectory(platform)
add_subdirectory(reflect)
add_subdirectory(render_bridge)
add_subdirectory(render_graph)
add_subdirectory(render_passes)
add_subdirectory(renderer)
add_subdirectory(rhi)
add_subdirectory(serialize)
add_subdirectory(world)

# aegis_add_device_test(<name> ARGS <aegis-test args> ... [LABELS <label> ...])
# Registers an aegis-test run that needs a GPU with a Vulkan driver (`ctest -LE device` skips it). It
# exits non-zero on any failed check or validation error. aegis-test returns 77 when the device lacks a
# feature the run needs, which CTest reports as skipped. Extra LABELS name that feature (`mesh-shaders`).
# The `gpu` resource lock keeps device runs from overlapping when ctest runs tests in parallel.
set(AEGIS_TEST_SKIP_RETURN_CODE 77) # Keep in step with tests/device/test.cppm
function(aegis_add_device_test name)
    cmake_parse_arguments(T "" "" "ARGS;LABELS" ${ARGN})
    add_test(NAME ${name} COMMAND aegis-test ${T_ARGS})
    set_tests_properties(
        ${name}
        PROPERTIES
            LABELS "device;${T_LABELS}"
            RESOURCE_LOCK gpu
            TIMEOUT 300
            SKIP_RETURN_CODE ${AEGIS_TEST_SKIP_RETURN_CODE}
    )
endfunction()

# The device tests load compiled shaders, so they exist only when the shaders are built
# (-DCOMPILE_SHADERS=OFF registers none). The ones that load the sample project exist only when it is checked out
# (AEGIS_SAMPLES); without it the harness skips its sample checks (--no-samples).
if(TARGET compile_shaders)
    if(AEGIS_SAMPLES)
        set(samples_args)
    else()
        set(samples_args --no-samples)
    endif()

    # The harness headless for a fixed number of frames.
    aegis_add_device_test(aegis-test-headless ARGS --headless --frames 600 ${samples_args})

    # Reference images: the last frame against tests/device/golden, generated on the dev GPU (see tests/README.md). Local
    # only: the lavapipe CI job runs the other device tests but excludes these with -LE golden, since software
    # rendering differs from the dev GPU (the triangle by 1 per channel, the scenes by up to 176 on ~80k pixels:
    # beyond any tolerance that would still catch a regression). The stats window is left out, its timings
    # change every run.
    aegis_add_device_test(
        aegis-test-golden-triangle
        ARGS
            --headless
            --frames
            120
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/triangle.png
            --tolerance
            0
            ${samples_args}
        LABELS golden
    )
endif()

if(TARGET compile_shaders AND AEGIS_SAMPLES)
    # The same harness rendering a loaded scene through the render graph (aegis.render_graph).
    aegis_add_device_test(aegis-test-scene ARGS --headless --scene --frames 120)
    aegis_add_device_test(
        aegis-test-scene-gpu-driven
        ARGS --headless --gpu-driven --frames 120
        LABELS mesh-shaders
    )

    # The scene changes while frames are in flight: a model loaded, one destroyed, materials edited.
    aegis_add_device_test(
        aegis-test-scene-mutation
        ARGS --headless --scene --mutate-scene --frames 150
    )
    aegis_add_device_test(
        aegis-test-scene-gpu-driven-mutation
        ARGS --headless --gpu-driven --mutate-scene --frames 150
        LABELS mesh-shaders
    )

    # An unlit cube next to a PBR one: the last frame shows its colour unlit, on both geometry paths.
    aegis_add_device_test(
        aegis-test-scene-unlit
        ARGS --headless --scene --unlit --no-overlay --frames 30
    )
    aegis_add_device_test(
        aegis-test-scene-gpu-driven-unlit
        ARGS --headless --gpu-driven --unlit --no-overlay --frames 30
        LABELS mesh-shaders
    )

    # An unlit, double-sided plane seen from behind: the last frame shows its colour, on both paths.
    aegis_add_device_test(
        aegis-test-scene-double-sided
        ARGS --headless --scene --double-sided --no-overlay --frames 30
    )
    aegis_add_device_test(
        aegis-test-scene-gpu-driven-double-sided
        ARGS --headless --gpu-driven --double-sided --no-overlay --frames 30
        LABELS mesh-shaders
    )

    # Two overlapping half-transparent cubes in front of the helmet: the forward pass draws them, the G-buffer does not,
    # on both paths.
    aegis_add_device_test(
        aegis-test-scene-blend
        ARGS --headless --scene --blend --frames 120
    )
    aegis_add_device_test(
        aegis-test-scene-gpu-driven-blend
        ARGS --headless --gpu-driven --blend --frames 120
        LABELS mesh-shaders
    )

    # The scene through a second camera into a view of its own size, resized apart from the target.
    aegis_add_device_test(
        aegis-test-scene-view
        ARGS --headless --scene --view --frames 120
    )
    aegis_add_device_test(
        aegis-test-scene-gpu-driven-view
        ARGS --headless --gpu-driven --view --frames 120
        LABELS mesh-shaders
    )

    # Two views of different sizes in one frame, each matching itself rendered alone; culling keeps a cube in one only.
    aegis_add_device_test(
        aegis-test-scene-views
        ARGS --headless --scene --views --no-overlay --frames 60
    )
    aegis_add_device_test(
        aegis-test-scene-gpu-driven-views
        ARGS --headless --gpu-driven --views --no-overlay --frames 60
        LABELS mesh-shaders
    )

    # A second world from the scene file, without the helmet, renders in the middle third of the frames.
    aegis_add_device_test(
        aegis-test-scene-worlds
        ARGS --headless --scene --worlds --frames 120
    )
    aegis_add_device_test(
        aegis-test-scene-gpu-driven-worlds
        ARGS --headless --gpu-driven --worlds --frames 120
        LABELS mesh-shaders
    )

    # The main camera's PostProcess, kept when the frame graph is rebuilt halfway.
    aegis_add_device_test(
        aegis-test-scene-post-process
        ARGS --headless --scene --post-process --frames 120
    )

    # The audio scene (#465): its looping AudioSource starts once on begin and still plays after the last frame. Headless
    # runs mix without an audio device, so this runs the same on every machine.
    aegis_add_device_test(aegis-test-audio ARGS --headless --audio --frames 120)

    # The scene's reference images (see above).
    aegis_add_device_test(
        aegis-test-golden-scene
        ARGS
            --headless
            --scene
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene.png
            --tolerance
            0
        LABELS golden
    )
    aegis_add_device_test(
        aegis-test-golden-scene-gpu-driven
        ARGS
            --headless
            --gpu-driven
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-gpu-driven.png
            --tolerance
            0
        LABELS golden mesh-shaders
    )
    # The helmet mirrored (a scale of -1 on x): it must look like the plain one, not inside out (#171).
    aegis_add_device_test(
        aegis-test-golden-scene-mirrored
        ARGS
            --headless
            --scene
            --mirror
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-mirrored.png
            --tolerance
            0
        LABELS golden
    )
    aegis_add_device_test(
        aegis-test-golden-scene-mirrored-gpu-driven
        ARGS
            --headless
            --gpu-driven
            --mirror
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-mirrored-gpu-driven.png
            --tolerance
            0
        LABELS golden mesh-shaders
    )
    # The two overlapping half-transparent cubes in front of the helmet, lit by the forward pass and composed back to
    # front: with --blend-swap they trade places, so a fixed draw order composes one of the two images wrongly.
    aegis_add_device_test(
        aegis-test-golden-scene-blend
        ARGS
            --headless
            --scene
            --blend
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-blend.png
            --tolerance
            0
        LABELS golden
    )
    aegis_add_device_test(
        aegis-test-golden-scene-blend-gpu-driven
        ARGS
            --headless
            --gpu-driven
            --blend
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-blend-gpu-driven.png
            --tolerance
            0
        LABELS golden mesh-shaders
    )
    aegis_add_device_test(
        aegis-test-golden-scene-blend-swap
        ARGS
            --headless
            --scene
            --blend-swap
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-blend-swap.png
            --tolerance
            0
        LABELS golden
    )
    aegis_add_device_test(
        aegis-test-golden-scene-blend-swap-gpu-driven
        ARGS
            --headless
            --gpu-driven
            --blend-swap
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-blend-swap-gpu-driven.png
            --tolerance
            0
        LABELS golden mesh-shaders
    )
    # Rendered with the main camera's PostProcess after the graph was rebuilt: not the plain scene's image.
    aegis_add_device_test(
        aegis-test-golden-scene-post-process
        ARGS
            --headless
            --scene
            --post-process
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-post-process.png
            --tolerance
            0
        LABELS golden
    )
    # Back on the main world after the second one rendered, the last frame is the plain scene's.
    aegis_add_device_test(
        aegis-test-golden-scene-worlds
        ARGS
            --headless
            --scene
            --worlds
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene.png
            --tolerance
            0
        LABELS golden
    )
    aegis_add_device_test(
        aegis-test-golden-scene-worlds-gpu-driven
        ARGS
            --headless
            --gpu-driven
            --worlds
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/scene-gpu-driven.png
            --tolerance
            0
        LABELS golden mesh-shaders
    )
    # The same scene without a reference, so CI's lavapipe job builds the masked pipelines under validation.
    aegis_add_device_test(
        aegis-test-scene-sponza
        ARGS --headless --scene-file scenes/sponza.scene --frames 10 --no-overlay
    )
    aegis_add_device_test(
        aegis-test-scene-sponza-gpu-driven
        ARGS --headless --gpu-driven --scene-file scenes/sponza.scene --frames 10 --no-overlay
        LABELS mesh-shaders
    )
    # Sponza's masked, double-sided materials (foliage, #135): cut out by their alpha, both sides drawn.
    aegis_add_device_test(
        aegis-test-golden-sponza
        ARGS
            --headless
            --scene-file
            scenes/sponza.scene
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/sponza.png
            --tolerance
            0
        LABELS golden
    )
    aegis_add_device_test(
        aegis-test-golden-sponza-gpu-driven
        ARGS
            --headless
            --gpu-driven
            --scene-file
            scenes/sponza.scene
            --frames
            120
            --no-overlay
            --compare
            ${CMAKE_CURRENT_SOURCE_DIR}/device/golden/sponza-gpu-driven.png
            --tolerance
            0
        LABELS golden mesh-shaders
    )
endif()
