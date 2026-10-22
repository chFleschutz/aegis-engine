# The examples as one game (docs/editor-runtime-split.md): its project is the samples, whose scene files the
# examples are. Run one with --scene <path>; without it the game starts in the simple scene.
aegis_add_game(Examples
    SOURCES game.cpp
    PROJECT ${PROJECT_SOURCE_DIR}/modules/aegis-assets
    SCENES Scenes/simple.scene
)
