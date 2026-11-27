# The editor's model: selection, the shared context, the editor camera, picking, the inspector's property model, the edits and undo history, scene files and their autosaves, and the material files the Material panel edits. No ImGui, no GPU; tested by aegis-tests-editor.
add_library(aegis-editor-core STATIC)

target_link_libraries(
    aegis-editor-core
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

target_sources(
    aegis-editor-core
    PUBLIC
        FILE_SET CXX_MODULES
            FILES
                core.cppm
                asset_browser.cppm
                autosave.cppm
                chord.cppm
                color.cppm
                console.cppm
                commands.cppm
                component_text.cppm
                context.cppm
                drawers.cppm
                edits.cppm
                error.cppm
                full_viewport.cppm
                editor_camera.cppm
                hierarchy.cppm
                inspector.cppm
                material_document.cppm
                picking.cppm
                play.cppm
                preferences.cppm
                property_model.cppm
                recent_scenes.cppm
                scene_document.cppm
                scene_file.cppm
                selection.cppm
                side_rail.cppm
                settled_extent.cppm
                undo_stack.cppm
    PRIVATE
        asset_browser.cpp
        autosave.cpp
        chord.cpp
        color.cpp
        commands.cpp
        component_text.cpp
        console.cpp
        context.cpp
        drawers.cpp
        edits.cpp
        error.cpp
        full_viewport.cpp
        editor_camera.cpp
        hierarchy.cpp
        inspector.cpp
        material_document.cpp
        picking.cpp
        play.cpp
        preferences.cpp
        property_model.cpp
        recent_scenes.cpp
        scene_document.cpp
        scene_file.cpp
        selection.cpp
        side_rail.cpp
        settled_extent.cpp
        undo_stack.cpp
)
