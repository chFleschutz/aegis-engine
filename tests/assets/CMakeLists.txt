aegis_add_test(assets
    SOURCES
        asset_database.test.cpp
        asset_id.test.cpp
        asset_ref.test.cpp
        asset_key.test.cpp
        defaults.test.cpp
        error.test.cpp
        gpu_types.test.cpp
        image_data.test.cpp
        material.test.cpp
        mesh_preprocessor.test.cpp
        model_import.test.cpp
        sidecar.test.cpp
        sub_asset_key.test.cpp
    LIBS aegis-assets
)

# gpu_types.test.cpp compares constants with the Slang sources they mirror.
target_compile_definitions(aegis-tests-assets PRIVATE AEGIS_TEST_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/shaders")

# sidecar.test.cpp reads the golden sidecar.
target_compile_definitions(aegis-tests-assets PRIVATE AEGIS_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

# asset_database.test.cpp checks the engine content constants against the content folder.
target_compile_definitions(aegis-tests-assets PRIVATE AEGIS_TEST_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content")
