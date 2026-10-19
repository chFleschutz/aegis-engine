add_library(aegis-render-graph STATIC)

target_link_libraries(aegis-render-graph
    PUBLIC
        aegis-assets
        aegis-math
        aegis-paths
        aegis-renderer
        aegis-rhi
        aegis-scene
        EnTT::EnTT # The draw batch registry follows scene component signals
    PRIVATE
        imgui
        vulkan-module # The UI pass hands raw Vulkan handles to ImGui's backend
)

target_sources(aegis-render-graph
    PUBLIC
    FILE_SET CXX_MODULES FILES
    render_graph.cppm
    default_graph.cppm
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
    passes/geometry_pass.cppm
    passes/gpu_driven.cppm
    passes/lighting_pass.cppm
    passes/post_effects.cppm
    passes/render_system.cppm
    passes/scene_update_pass.cppm
    passes/screen_passes.cppm
    passes/static_mesh_render_system.cppm
    passes/ui_pass.cppm
    PRIVATE
    default_graph.cpp
    draw_batch_registry.cpp
    frame_graph.cpp
    ibl.cpp
    resource_pool.cpp
    schedule.cpp
    shader.cpp
    passes/geometry_pass.cpp
    passes/gpu_driven.cpp
    passes/lighting_pass.cpp
    passes/post_effects.cpp
    passes/scene_update_pass.cpp
    passes/screen_passes.cpp
    passes/static_mesh_render_system.cpp
    passes/ui_pass.cpp
)
