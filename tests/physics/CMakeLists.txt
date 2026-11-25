aegis_add_test(
    physics
    SOURCES
        character.test.cpp
        components.test.cpp
        contacts.test.cpp
        physics_system.test.cpp
        queries.test.cpp
    LIBS aegis-physics aegis-scripting aegis-serialize aegis-world aegis-reflect aegis-math
)
