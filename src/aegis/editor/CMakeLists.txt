# The editor's model: selection, the shared context, the editor camera, picking and the inspector's property model. No ImGui, no GPU; tested by aegis-tests-editor.
add_library(aegis-editor-core STATIC)

target_link_libraries(aegis-editor-core
    PUBLIC
        aegis-core
        aegis-math
        aegis-reflect
        aegis-world
    PRIVATE
        glaze::glaze
)

target_sources(aegis-editor-core
    PUBLIC
    FILE_SET CXX_MODULES FILES
    core/core.cppm
    core/console.cppm
    core/context.cppm
    core/editor_camera.cppm
    core/inspector.cppm
    core/picking.cppm
    core/preferences.cppm
    core/property_model.cppm
    core/selection.cppm
    core/settled_extent.cppm
    core/undo_stack.cppm
    PRIVATE
    core/console.cpp
    core/context.cpp
    core/editor_camera.cpp
    core/inspector.cpp
    core/picking.cpp
    core/preferences.cpp
    core/property_model.cpp
    core/selection.cpp
    core/settled_extent.cpp
    core/undo_stack.cpp
)

# The editor's ImGui layer: EditorShell and the panels. Only a game's <Game>-Editor links it (aegis_add_game).
add_library(aegis-editor STATIC)

target_link_libraries(aegis-editor
    PUBLIC
        aegis-editor-core
        aegis-engine # The shell it implements
    PRIVATE
        aegis-asset-library # The inspector's asset slots
        aegis-assets
        aegis-core
        aegis-math
        aegis-reflect
        aegis-render-graph
        aegis-render-passes # The frame graph's resource names
        aegis-renderer
        aegis-rhi
        aegis-scripting
        aegis-serialize # InstanceMember
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
    ui/viewport.cppm
    PRIVATE
    ui/editor_shell.cpp
    ui/panels/console_panel.cpp
    ui/panels/preferences_panel.cpp
    ui/panels/demo_panel.cpp
    ui/panels/history.cpp
    ui/panels/inspector_panel.cpp
    ui/panels/profiler_panel.cpp
    ui/panels/renderer_panel.cpp
    ui/panels/scene_panel.cpp
    ui/panels/statistics_panel.cpp
    ui/viewport.cpp
)
