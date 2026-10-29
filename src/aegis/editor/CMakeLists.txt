# The editor's model: selection, the shared context, the editor camera, picking, the inspector's property model, and the edits and undo history. No ImGui, no GPU; tested by aegis-tests-editor.
add_library(aegis-editor-core STATIC)

target_link_libraries(aegis-editor-core
    PUBLIC
        aegis-core
        aegis-math
        aegis-reflect
        aegis-serialize # Subtree snapshots of the structural edits, the play copy and scene files
        aegis-world
    PRIVATE
        aegis-scripting # Attaching and detaching scripts
        glaze::glaze
)

target_sources(aegis-editor-core
    PUBLIC
    FILE_SET CXX_MODULES FILES
    core/core.cppm
    core/asset_browser.cppm
    core/chord.cppm
    core/console.cppm
    core/commands.cppm
    core/component_text.cppm
    core/context.cppm
    core/edits.cppm
    core/editor_camera.cppm
    core/hierarchy.cppm
    core/inspector.cppm
    core/picking.cppm
    core/play.cppm
    core/preferences.cppm
    core/property_model.cppm
    core/scene_file.cppm
    core/selection.cppm
    core/settled_extent.cppm
    core/undo_stack.cppm
    PRIVATE
    core/asset_browser.cpp
    core/chord.cpp
    core/commands.cpp
    core/component_text.cpp
    core/console.cpp
    core/context.cpp
    core/edits.cpp
    core/editor_camera.cpp
    core/hierarchy.cpp
    core/inspector.cpp
    core/picking.cpp
    core/play.cpp
    core/preferences.cpp
    core/property_model.cpp
    core/scene_file.cpp
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
        aegis-paths # The engine content's fonts
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
    ui/actions.cppm
    ui/editor.cppm
    ui/editor_shell.cppm
    ui/panel.cppm
    ui/panels.cppm
    ui/theme.cppm
    ui/viewport.cppm
    PRIVATE
    ui/actions.cpp
    ui/editor_shell.cpp
    ui/panels/all_entities_panel.cpp
    ui/panels/asset_panel.cpp
    ui/panels/console_panel.cpp
    ui/panels/preferences_panel.cpp
    ui/panels/demo_panel.cpp
    ui/panels/hint.cpp
    ui/panels/history.cpp
    ui/panels/inspector_panel.cpp
    ui/panels/menus.cpp
    ui/panels/profiler_panel.cpp
    ui/panels/renderer_panel.cpp
    ui/panels/scene_panel.cpp
    ui/panels/statistics_panel.cpp
    ui/theme.cpp
    ui/viewport.cpp
)
