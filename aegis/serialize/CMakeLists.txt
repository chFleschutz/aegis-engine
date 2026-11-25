# Native files (scenes, materials) through the type registry.
add_library(aegis-serialize STATIC)

# PUBLIC: the module interfaces import aegis.world, aegis.assets (AssetRef) and aegis.reflect. PRIVATE: glaze
# and xxHash stay in .cpp files, and only the implementation attaches scripts and logs.
target_link_libraries(
    aegis-serialize
    PUBLIC aegis-assets aegis-reflect aegis-world
    PRIVATE aegis-core aegis-scripting glaze::glaze xxhash
)

target_sources(
    aegis-serialize
    PUBLIC
        FILE_SET CXX_MODULES
            FILES
                serialize.cppm
                document.cppm
                error.cppm
                json.cppm
                material.cppm
                material_template.cppm
                migration.cppm
                model.cppm
                scene.cppm
    PRIVATE
        document.cpp
        error.cpp
        json.cpp
        material.cpp
        material_template.cpp
        migration.cpp
        model.cpp
        scene.cpp
)
