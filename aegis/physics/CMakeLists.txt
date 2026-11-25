add_library(aegis-physics STATIC)

# The implementation files: each compiles with Jolt's flags (below), so any of them may include Jolt.
set(PHYSICS_SOURCES components.cpp physics_system.cpp)

target_sources(
    aegis-physics
    PUBLIC
        FILE_SET CXX_MODULES
            BASE_DIRS "${AEGIS_MODULE_ROOT}"
            FILES components.cppm contacts.cppm physics.cppm physics_system.cppm queries.cppm
    PRIVATE ${PHYSICS_SOURCES}
)

# PUBLIC: the module interfaces import aegis.math, aegis.reflect and aegis.world.
# PRIVATE: Jolt's headers are included only in the .cpp files; its public defines reach them through this link.
target_link_libraries(
    aegis-physics
    PUBLIC aegis-math aegis-reflect aegis-world
    PRIVATE aegis-core Jolt
)

# Jolt's instruction-set flags go on every implementation file, not on the target: the module interfaces, which include
# no Jolt, keep the flags of their importers (see external/CMakeLists.txt). A .cpp that includes Jolt without them
# would build its inline code for another instruction set than physics_system.cpp, with no error.
get_target_property(JOLT_SOURCE_OPTIONS Jolt JOLT_SOURCE_COMPILE_OPTIONS)
set_source_files_properties(${PHYSICS_SOURCES} PROPERTIES COMPILE_OPTIONS "${JOLT_SOURCE_OPTIONS}")
