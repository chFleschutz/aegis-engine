aegis_add_test(render_bridge
    SOURCES
        extract.test.cpp
        instance_bridge.test.cpp
        worlds.test.cpp
    LIBS aegis-render-bridge aegis-serialize # worlds.test.cpp copies a scene into two worlds
)
