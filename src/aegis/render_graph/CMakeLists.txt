add_library(aegis-render-graph STATIC)

target_link_libraries(aegis-render-graph
    PUBLIC
        aegis-assets
        aegis-math
        aegis-renderer
        aegis-rhi
        aegis-scene
        EnTT::EnTT # The draw batch registry follows scene component signals
    PRIVATE
        imgui
)

# Compiled shaders are loaded at run time from the build tree (see shaders/CMakeLists.txt).
target_compile_definitions(aegis-render-graph PRIVATE AEGIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")

target_sources(aegis-render-graph
    PUBLIC
    FILE_SET CXX_MODULES FILES
    render_graph.cppm
    draw_batch_registry.cppm
    error.cppm
    frame_graph.cppm
    frustum.cppm
    ibl.cppm
    immediate.cppm
    render_pass.cppm
    resource.cppm
    resource_pool.cppm
    schedule.cppm
    shader.cppm
    PRIVATE
    draw_batch_registry.cpp
    frame_graph.cpp
    ibl.cpp
    resource_pool.cpp
    schedule.cpp
    shader.cpp
)
