# The editor's ImGui layer: EditorShell and the panels. Only a game's <Game>-Editor links it (aegis_add_game).
add_library(aegis-editor STATIC)

target_link_libraries(
    aegis-editor
    PUBLIC
        aegis-editor-core
        # The shell it implements
        aegis-engine
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

# The version Help > About shows
target_compile_definitions(aegis-editor PRIVATE AEGIS_VERSION="${AegisEngine_VERSION}")

target_sources(
    aegis-editor
    PUBLIC
        FILE_SET CXX_MODULES
            FILES
                actions.cppm
                edits.cppm
                editor.cppm
                editor_shell.cppm
                lucide.cppm
                options.cppm
                panel.cppm
                panels.cppm
                statistics.cppm
                theme.cppm
                viewport.cppm
                widgets.cppm
                workspace.cppm
    PRIVATE
        actions.cpp
        create_menu.cpp
        edits.cpp
        editor_shell.cpp
        hint.cpp
        options.cpp
        panels/about_panel.cpp
        panels/all_entities_panel.cpp
        panels/content_panel.cpp
        panels/console_panel.cpp
        panels/preferences_panel.cpp
        panels/inspector_panel.cpp
        panels/material_panel.cpp
        panels/profiler_panel.cpp
        panels/renderer_panel.cpp
        panels/scene_panel.cpp
        panels/shortcuts_panel.cpp
        panels/statistics_panel.cpp
        statistics.cpp
        theme.cpp
        viewport.cpp
        widgets.cpp
        workspace.cpp
)
