add_library(aegis-assets STATIC)

target_link_libraries(
    aegis-assets
    PUBLIC
        aegis-core
        aegis-rhi
        aegis-math
        # AssetRef is a reflected field kind
        aegis-reflect
    PRIVATE fastgltf glaze::glaze ktx meshoptimizer stb tinyobjloader xxhash
)

target_sources(
    aegis-assets
    PUBLIC
        FILE_SET CXX_MODULES
            FILES
                assets.cppm
                defaults.cppm
                environment.cppm
                loader.cppm
                material.cppm
                static_mesh.cppm
                texture.cppm
                upload.cppm
                data/asset_database.cppm
                data/asset_id.cppm
                data/asset_key.cppm
                data/asset_ref.cppm
                data/data.cppm
                data/embedded_images.cppm
                data/engine_content.cppm
                data/error.cppm
                data/external_images.cppm
                data/gpu_types.cppm
                data/image_data.cppm
                data/import_cache.cppm
                data/mesh_blob.cppm
                data/mesh_data.cppm
                data/mesh_preprocessor.cppm
                data/model_data.cppm
                data/sidecar.cppm
                data/sub_asset_key.cppm
                data/texture_blob.cppm
                data/texture_data.cppm
    PRIVATE
        defaults.cpp
        loader.cpp
        material.cpp
        static_mesh.cpp
        texture.cpp
        data/asset_database.cpp
        data/asset_id.cpp
        data/embedded_images.cpp
        data/external_images.cpp
        data/gltf_import.cpp
        data/image_data.cpp
        data/import_cache.cpp
        data/mesh_blob.cpp
        data/mesh_preprocessor.cpp
        data/model_import.cpp
        data/obj_import.cpp
        data/sidecar.cpp
        data/sub_asset_key.cpp
        data/texture_blob.cpp
        data/texture_data.cpp
)
