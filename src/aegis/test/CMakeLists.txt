add_executable(aegis-test main.cpp)

target_link_libraries(aegis-test
    PRIVATE
        aegis-assets
        aegis-math
        aegis-platform
        aegis-render-graph
        aegis-renderer
        aegis-rhi
        aegis-scene
        imgui
        stb # stb_image_write for --screenshot
)

aegis_add_shaders(test-shaders SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shader.slang)
set(SHADER_SPIRV "${CMAKE_CURRENT_BINARY_DIR}/shader.slang.spv")
add_dependencies(aegis-test test-shaders)
target_compile_definitions(aegis-test PUBLIC
    SHADER_PATH="${SHADER_SPIRV}"
    ASSETS_DIR="${PROJECT_SOURCE_DIR}/modules/aegis-assets"
)
