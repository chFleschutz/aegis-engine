# The engine's passes, the frame scene they draw, IBL and the shader loader (module
# aegis.render_passes). Depends on compile_shaders (shaders/CMakeLists.txt).
add_library(aegis-render-passes STATIC)

target_link_libraries(
    aegis-render-passes
    PUBLIC aegis-assets aegis-core aegis-math aegis-render-graph aegis-renderer aegis-rhi
    PRIVATE aegis-paths imgui
)

target_sources(
    aegis-render-passes
    PUBLIC
        FILE_SET CXX_MODULES
            FILES
                render_passes.cppm
                default_graph.cppm
                draw_order.cppm
                forward_pass.cppm
                frame_scene.cppm
                geometry_pass.cppm
                gpu_driven.cppm
                gpu_types.cppm
                ibl.cppm
                instance_store.cppm
                lighting_pass.cppm
                names.cppm
                pipeline_cache.cppm
                post_effects.cppm
                render_system.cppm
                scene_update_pass.cppm
                screen_passes.cppm
                shader.cppm
                shadows.cppm
                static_mesh_render_system.cppm
                ui_pass.cppm
    PRIVATE
        default_graph.cpp
        draw_order.cpp
        forward_pass.cpp
        geometry_pass.cpp
        gpu_driven.cpp
        ibl.cpp
        instance_store.cpp
        lighting_pass.cpp
        post_effects.cpp
        scene_update_pass.cpp
        screen_passes.cpp
        shader.cpp
        shadows.cpp
        static_mesh_render_system.cpp
        ui_pass.cpp
)
