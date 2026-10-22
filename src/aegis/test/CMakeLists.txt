add_executable(aegis-test)

target_sources(aegis-test
    PRIVATE
    FILE_SET CXX_MODULES FILES
    test.cppm
    PRIVATE
    main.cpp
    engine.cpp
    checks/assets.cpp
    checks/graph.cpp
    checks/ibl.cpp
    checks/input.cpp
    checks/offscreen.cpp
    checks/rhi.cpp
    checks/scene.cpp
    checks/uploads.cpp
)

target_link_libraries(aegis-test
    PRIVATE
        aegis-asset-library
        aegis-assets
        aegis-base
        aegis-math
        aegis-paths
        aegis-platform
        aegis-reflect
        aegis-render-bridge
        aegis-render-graph
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
