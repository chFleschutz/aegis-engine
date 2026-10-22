# The examples as one game (docs/editor-runtime-split.md). Its project is the samples, and the examples are
# their scene files: run one with --scene <path>, or without it the simple scene. Each scene listed here is a
# `device` test; the stress scenes (stress/: instanced_helmets, cubes, dynamic_cubes) are left out, too slow on lavapipe.
aegis_add_game(Examples
    SOURCES game.cpp spawner.cpp
    PROJECT ${PROJECT_SOURCE_DIR}/samples
    SCENES scenes/simple.scene scenes/sponza.scene scenes/helmets.scene
)
