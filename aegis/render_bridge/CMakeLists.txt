# Between the world and the render graph: the instance store that follows the world, the per-frame FrameScene, and
# the worlds an app holds, each with its own store. Its own target so aegis-test renders a world through it without
# the engine.
add_library(aegis-render-bridge STATIC)
target_link_libraries(
    aegis-render-bridge
    PUBLIC
        aegis-render-passes # FrameScene and InstanceStore
        aegis-world
        EnTT::EnTT # Follows the world's component signals
    PRIVATE aegis-assets aegis-core aegis-math aegis-rhi
)
target_sources(
    aegis-render-bridge
    PUBLIC FILE_SET CXX_MODULES FILES render_bridge.cppm
    PRIVATE render_bridge.cpp
)
