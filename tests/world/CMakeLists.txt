aegis_add_test(world
    SOURCES
        frame_phases.test.cpp
        kinematic_movement_controller.test.cpp
        registry.test.cpp
        script_system.test.cpp
        world.test.cpp
        stable_id.test.cpp
        transform_system.test.cpp
    LIBS aegis-world aegis-scripting aegis-reflect
)
