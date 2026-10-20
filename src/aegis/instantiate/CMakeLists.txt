add_library(aegis-instantiate STATIC)

# PUBLIC: the module interface imports aegis.assets and the aegis.world.* modules.
target_link_libraries(aegis-instantiate
    PUBLIC
        aegis-assets
        aegis-math
        aegis-world
)

target_sources(aegis-instantiate
    PUBLIC
    FILE_SET CXX_MODULES FILES
    instantiate.cppm
    PRIVATE
    instantiate.cpp
)
