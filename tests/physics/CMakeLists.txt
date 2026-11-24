aegis_add_test(
    physics
    SOURCES components.test.cpp physics_system.test.cpp
    LIBS aegis-physics aegis-serialize aegis-world aegis-reflect aegis-math
)
