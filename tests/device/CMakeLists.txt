add_executable(aegis-test)

target_sources(
    aegis-test
    PRIVATE FILE_SET CXX_MODULES FILES test.cppm
    PRIVATE
        main.cpp
        harness.cpp
        checks/assets.cpp
        checks/gameplay.cpp
        checks/graph.cpp
        checks/ibl.cpp
        checks/input.cpp
        checks/material_file.cpp
        checks/offscreen.cpp
        checks/rhi.cpp
        checks/scene.cpp
        checks/uploads.cpp
        checks/window.cpp
)

target_link_libraries(
    aegis-test
    PRIVATE
        aegis-asset-library
        aegis-assets
        aegis-core
        aegis-engine # --scene runs through the app
        aegis-math
        aegis-paths
        aegis-physics # --physics reads the scene's rigid bodies
        aegis-platform
        aegis-reflect
        aegis-render-bridge
        aegis-render-graph
        aegis-render-passes
        aegis-renderer
        aegis-rhi
        aegis-scripting # --spawn's script
        aegis-serialize # --material-file's edited copy of the file
        aegis-world
        imgui
        stb # stb_image_write for --screenshot
)

# --gameplay plays the samples' gameplay scene with the Examples game's scripts: its module's objects come along.
if(AEGIS_SAMPLES)
    target_link_libraries(aegis-test PRIVATE Examples-module)
    target_compile_definitions(aegis-test PRIVATE AEGIS_TEST_EXAMPLES)
endif()

# accumulate.slang reaches storage images through the engine's bindless module.
aegis_add_shaders(
    test-shaders
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shader.slang ${CMAKE_CURRENT_SOURCE_DIR}/accumulate.slang
    MODULES ${CMAKE_SOURCE_DIR}/shaders/modules/bindless.slang
    INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/shaders
)
set(SHADER_SPIRV "${CMAKE_CURRENT_BINARY_DIR}/shader.slang.spv")
add_dependencies(aegis-test test-shaders)
# The test's own fixture shaders, built next to it (not engine shaders, see aegis.paths).
target_compile_definitions(
    aegis-test
    PRIVATE
        AEGIS_TEST_SHADER_PATH="${SHADER_SPIRV}"
        AEGIS_TEST_ACCUMULATE_SHADER_PATH="${CMAKE_CURRENT_BINARY_DIR}/accumulate.slang.spv"
)
