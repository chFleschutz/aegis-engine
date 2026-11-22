aegis_add_test(
    render_passes
    SOURCES
        default_graph.test.cpp
        draw_order.test.cpp
        frame_data.test.cpp
        frame_scene_source.test.cpp
        gpu_driven.test.cpp
        gpu_types.test.cpp
        instance_store.test.cpp
        pipeline_cache.test.cpp
        shadows.test.cpp
    LIBS aegis-render-passes
)

# The checked-in plans of the default graphs (see default_graph.test.cpp).
target_compile_definitions(
    aegis-tests-render_passes
    PRIVATE AEGIS_PLAN_SNAPSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/snapshots"
)
