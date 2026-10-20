add_library(aegis-assets STATIC)

target_link_libraries(aegis-assets
    PUBLIC
        aegis-base
        aegis-rhi
        aegis-core  # aegis::core::Asset, the base class of the loaded assets
        aegis-math
    PRIVATE
        fastgltf
        meshoptimizer
        stb
        tinyobjloader
)

target_sources(aegis-assets
    PUBLIC
    FILE_SET CXX_MODULES FILES
    asset_key.cppm
    assets.cppm
    components.cppm
    defaults.cppm
    error.cppm
    gpu_types.cppm
    image_data.cppm
    model_data.cppm
    loader.cppm
    material.cppm
    mesh_data.cppm
    mesh_preprocessor.cppm
    static_mesh.cppm
    texture.cppm
    upload.cppm
    PRIVATE
    defaults.cpp
    gltf_import.cpp
    image_data.cpp
    loader.cpp
    material.cpp
    mesh_preprocessor.cpp
    model_import.cpp
    obj_import.cpp
    static_mesh.cpp
    texture.cpp
)
