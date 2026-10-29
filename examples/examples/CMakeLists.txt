# The examples as one game (docs/editor-runtime-split.md). Its project is the samples, and the examples are
# their scene files: run one with --scene <path>, or without it the simple scene. Only the smoke scene is a
# `device` test: one scene with a large model, scene instances, the sky, scripts, static and dynamic entities
# and a changing light, so the startup and asset load are paid once. The other scenes stay demos (built, not run).
aegis_add_game(Examples
    SOURCES game.cpp spawner.cpp
    PROJECT ${PROJECT_SOURCE_DIR}/samples
    SCENES scenes/smoke.scene
    EDITOR_SELECT "Pulsing Light" # A point light with a script: its Inspector shows both
)
