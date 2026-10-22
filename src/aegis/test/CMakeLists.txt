add_executable(aegis-test)

target_sources(aegis-test
    PRIVATE
    FILE_SET CXX_MODULES FILES
    test.cppm
    PRIVATE
    main.cpp
    harness.cpp
    checks/assets.cpp
    checks/graph.cpp
    checks/ibl.cpp
    checks/input.cpp
    checks/offscreen.cpp
    checks/rhi.cpp
    checks/scene.cpp
    checks/uploads.cpp
    checks/window.cpp
)

target_link_libraries(aegis-test
    PRIVATE
        aegis-asset-library
        aegis-assets
        aegis-core
        aegis-engine # --scene runs through the app
        aegis-math
        aegis-paths
        aegis-platform
        aegis-reflect
        aegis-render-bridge
        aegis-render-graph
        aegis-render-passes
        aegis-renderer
        aegis-rhi
        aegis-world
        imgui
        stb # stb_image_write for --screenshot
)

aegis_add_shaders(test-shaders SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shader.slang)
set(SHADER_SPIRV "${CMAKE_CURRENT_BINARY_DIR}/shader.slang.spv")
add_dependencies(aegis-test test-shaders)
# The test's own fixture shader, built next to it (not an engine shader, see aegis.paths).
target_compile_definitions(aegis-test PRIVATE
    AEGIS_TEST_SHADER_PATH="${SHADER_SPIRV}"
)
