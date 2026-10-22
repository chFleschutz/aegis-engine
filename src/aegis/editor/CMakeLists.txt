# The editor's model: selection and the shared context. No ImGui, no GPU; tested by aegis-tests-editor.
add_library(aegis-editor-core STATIC)

target_link_libraries(aegis-editor-core
    PUBLIC
        aegis-world
)

target_sources(aegis-editor-core
    PUBLIC
    FILE_SET CXX_MODULES FILES
    core/core.cppm
    core/context.cppm
    core/selection.cppm
    PRIVATE
    core/context.cpp
    core/selection.cpp
)

# The editor's ImGui layer: EditorShell and the panels. Only a game's <Game>-Editor links it (aegis_add_game).
add_library(aegis-editor STATIC)

target_link_libraries(aegis-editor
    PUBLIC
        aegis-editor-core
        aegis-engine # The shell it implements
    PRIVATE
        aegis-base
        aegis-math
        aegis-render-graph
        aegis-renderer
        aegis-rhi
        aegis-world
        imgui::imgui
        imguizmo
)

target_sources(aegis-editor
    PUBLIC
    FILE_SET CXX_MODULES FILES
    ui/editor.cppm
    ui/editor_shell.cppm
    ui/panel.cppm
    ui/panels.cppm
    PRIVATE
    ui/editor_shell.cpp
    ui/panels/demo_panel.cpp
    ui/panels/profiler_panel.cpp
    ui/panels/renderer_panel.cpp
    ui/panels/scene_panel.cpp
    ui/panels/statistics_panel.cpp
)
