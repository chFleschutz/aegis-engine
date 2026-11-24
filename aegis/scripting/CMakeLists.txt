add_library(aegis-scripting STATIC)

# PUBLIC: the module interfaces import aegis.world, aegis.assets, aegis.math, aegis.platform and aegis.reflect.
# PRIVATE: the implementation imports aegis.core.

target_link_libraries(
    aegis-scripting
    PUBLIC aegis-assets aegis-math aegis-platform aegis-reflect aegis-world
    PRIVATE aegis-core
)

target_sources(
    aegis-scripting
    PUBLIC
        FILE_SET CXX_MODULES
            BASE_DIRS "${AEGIS_MODULE_ROOT}"
            FILES
                scripting.cppm
                script.cppm
                script_system.cppm
                movement/kinematic_movement_controller.cppm
    PRIVATE script.cpp script_system.cpp movement/kinematic_movement_controller.cpp
)

target_include_directories(aegis-scripting PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
