aegis_add_test(
    editor
    SOURCES
        asset_browser.test.cpp
        autosave.test.cpp
        chord.test.cpp
        color.test.cpp
        commands.test.cpp
        component_text.test.cpp
        console.test.cpp
        context.test.cpp
        drawers.test.cpp
        editor_camera.test.cpp
        edits.test.cpp
        error.test.cpp
        full_viewport.test.cpp
        hierarchy.test.cpp
        inspector.test.cpp
        material_document.test.cpp
        overrides.test.cpp
        picking.test.cpp
        play.test.cpp
        preferences.test.cpp
        property_model.test.cpp
        recent_scenes.test.cpp
        scene_document.test.cpp
        scene_file.test.cpp
        selection.test.cpp
        side_rail.test.cpp
        settled_extent.test.cpp
        undo_stack.test.cpp
    LIBS aegis-editor-core aegis-scripting
)

# The editor's ImGui layer (aegis.editor): only its pure parts, no ImGui context or device.
aegis_add_test(editor-ui SOURCES options.test.cpp LIBS aegis-editor)
