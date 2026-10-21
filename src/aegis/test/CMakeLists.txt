add_executable(aegis-test main.cpp)

target_link_libraries(aegis-test
    PRIVATE
        aegis-asset-library
        aegis-assets
        aegis-instantiate
        aegis-math
        aegis-paths
        aegis-platform
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
