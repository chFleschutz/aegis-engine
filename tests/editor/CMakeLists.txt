aegis_add_test(editor
    SOURCES
        console.test.cpp
        context.test.cpp
        editor_camera.test.cpp
        inspector.test.cpp
        picking.test.cpp
        preferences.test.cpp
        property_model.test.cpp
        selection.test.cpp
        settled_extent.test.cpp
        undo_stack.test.cpp
    LIBS aegis-editor-core
)
