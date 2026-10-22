aegis_add_test(serialize
    SOURCES
        json.test.cpp
        material.test.cpp
        model.test.cpp
        scene.test.cpp
        scene_instance.test.cpp
        subtree.test.cpp
    LIBS aegis-serialize aegis-scripting aegis-world aegis-reflect aegis-assets aegis-math aegis-base
)

# scene.test.cpp and material.test.cpp read the golden files.
target_compile_definitions(aegis-tests-serialize PRIVATE AEGIS_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
