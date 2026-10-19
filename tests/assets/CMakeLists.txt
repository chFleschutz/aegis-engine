aegis_add_test(assets
    SOURCES
        defaults.test.cpp
        gpu_types.test.cpp
        material.test.cpp
        mesh_preprocessor.test.cpp
    LIBS aegis-assets
)

# gpu_types.test.cpp compares constants with the Slang sources they mirror.
target_compile_definitions(aegis-tests-assets PRIVATE AEGIS_TEST_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/shaders")
