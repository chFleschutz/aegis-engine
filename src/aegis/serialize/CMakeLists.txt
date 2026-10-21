# Native files (scenes) through the type registry.
add_library(aegis-serialize STATIC)

# PUBLIC: the module interfaces import aegis.reflect and aegis.world. PRIVATE: glaze stays in .cpp files,
# and only the implementation attaches scripts.
target_link_libraries(aegis-serialize
    PUBLIC
        aegis-reflect
        aegis-world
    PRIVATE
        aegis-scripting
        glaze::glaze
)

target_sources(aegis-serialize
    PUBLIC
    FILE_SET CXX_MODULES FILES
    serialize.cppm
    error.cppm
    json.cppm
    scene.cppm
    PRIVATE
    error.cpp
    json.cpp
    scene.cpp
)
