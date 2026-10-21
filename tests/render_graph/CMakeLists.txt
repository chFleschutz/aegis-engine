aegis_add_test(render_graph
    SOURCES
        declarations.test.cpp
        default_graph.test.cpp
        error.test.cpp
        frame_data.test.cpp
        frustum.test.cpp
        gpu_driven.test.cpp
        gpu_types.test.cpp
        instance_store.test.cpp
        resource_usage.test.cpp
        schedule.test.cpp
        texture_extent.test.cpp
    LIBS aegis-render-graph
)

# The checked-in plans of the default graphs (see default_graph.test.cpp).
target_compile_definitions(aegis-tests-render_graph PRIVATE AEGIS_PLAN_SNAPSHOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/snapshots")
