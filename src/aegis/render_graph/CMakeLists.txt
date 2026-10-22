# The framework only: no scene, ImGui or shaders, so its unit tests stay cheap. The passes are
# aegis-render-passes (passes/).
add_library(aegis-render-graph STATIC)

target_link_libraries(aegis-render-graph
    PUBLIC
        aegis-assets
        aegis-math
        aegis-renderer
        aegis-rhi
    PRIVATE
        aegis-core
)

target_sources(aegis-render-graph
    PUBLIC
    FILE_SET CXX_MODULES FILES
    render_graph.cppm
    declarations.cppm
    error.cppm
    frame_graph.cppm
    frame_scene.cppm
    graph_builder.cppm
    frustum.cppm
    instance_store.cppm
    immediate.cppm
    render_pass.cppm
    resource.cppm
    resource_pool.cppm
    schedule.cppm
    PRIVATE
    declarations.cpp
    frame_graph.cpp
    instance_store.cpp
    resource_pool.cpp
    schedule.cpp
)

add_subdirectory(passes)
