aegis_add_test(scene
    SOURCES
        kinematic_movement_controller.test.cpp
        registry.test.cpp
        script_system.test.cpp
        scene.test.cpp
        stable_id.test.cpp
        transform_system.test.cpp
    LIBS aegis-scene aegis-scripting aegis-reflect
)
