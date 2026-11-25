aegis_add_test(
    serialize
    SOURCES
        json.test.cpp
        material.test.cpp
        material_template.test.cpp
        migration.test.cpp
        model.test.cpp
        scene.test.cpp
        scene_instance.test.cpp
        script_spawn.test.cpp
        subtree.test.cpp
    LIBS
        aegis-serialize
        aegis-scripting
        aegis-world
        aegis-reflect
        aegis-assets
        aegis-math
        aegis-core
)

# scene.test.cpp and material.test.cpp read the golden files.
target_compile_definitions(
    aegis-tests-serialize
    PRIVATE AEGIS_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

# material_template.test.cpp checks the engine content's templates against the layouts the build reflects from
# their surface shaders.
target_compile_definitions(
    aegis-tests-serialize
    PRIVATE AEGIS_TEST_CONTENT_DIR="${PROJECT_SOURCE_DIR}/content"
)
if(TARGET compile_shaders)
    target_compile_definitions(
        aegis-tests-serialize
        PRIVATE AEGIS_TEST_SHADER_DIR="${PROJECT_BINARY_DIR}/shaders"
    )
    add_dependencies(aegis-tests-serialize compile_shaders)
endif()
