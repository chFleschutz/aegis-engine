add_library(aegis-physics STATIC)

target_sources(
    aegis-physics
    PUBLIC
        FILE_SET CXX_MODULES
            BASE_DIRS "${AEGIS_MODULE_ROOT}"
            FILES components.cppm physics.cppm physics_system.cppm
    PRIVATE components.cpp physics_system.cpp
)

# PUBLIC: the module interfaces import aegis.math, aegis.reflect and aegis.world.
# PRIVATE: Jolt's headers are included only in the .cpp files; its public defines reach them through this link.
target_link_libraries(
    aegis-physics
    PUBLIC aegis-math aegis-reflect aegis-world
    PRIVATE aegis-core Jolt
)

# The one file that includes Jolt compiles with its instruction-set flags (see external/CMakeLists.txt).
get_target_property(JOLT_SOURCE_OPTIONS Jolt JOLT_SOURCE_COMPILE_OPTIONS)
set_source_files_properties(physics_system.cpp PROPERTIES COMPILE_OPTIONS "${JOLT_SOURCE_OPTIONS}")
