aegis_add_test(scene
    SOURCES
        registry.test.cpp
        script_system.test.cpp
        scene.test.cpp
        stable_id.test.cpp
        transform_system.test.cpp
    LIBS aegis-scene aegis-scripting aegis-reflect
)
