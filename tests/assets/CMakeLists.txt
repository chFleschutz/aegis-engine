aegis_add_test(assets
    SOURCES
        asset_key.test.cpp
        defaults.test.cpp
        error.test.cpp
        gpu_types.test.cpp
        image_data.test.cpp
        material.test.cpp
        mesh_preprocessor.test.cpp
        model_import.test.cpp
    LIBS aegis-assets
)

# gpu_types.test.cpp compares constants with the Slang sources they mirror.
target_compile_definitions(aegis-tests-assets PRIVATE AEGIS_TEST_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/shaders")
