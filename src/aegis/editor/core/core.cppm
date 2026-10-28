// aegis.editor.core: the editor's model, with no ImGui and no GPU (docs/editor.md).
//
// The ImGui layer (aegis.editor) reads and changes the world through it; everything here is testable on a
// plain world (aegis-tests-editor).
//
// Partitions:
//   :component_text The text a component's fields copy to the clipboard and paste from.
//   :asset_browser  AssetTree: the asset browser's folders, files and filter, and the asset drag payload.
//   :chord          Chord: a shortcut's key and modifiers.
//   :console        ConsoleBuffer: the log lines the console shows, with filters.
//   :preferences    EditorPreferences: the user's settings, kept in the config folder.
//   :selection      The selected entities, by StableId.
//   :context        EditorContext: the edited world and the selection, shared by the panels.
//   :picking        Rays and bounding spheres: what a click in the viewport selects.
//   :property_model PropertyRow: how the inspector shows a registered field (its widget, label and limits).
//   :inspector      The components and scripts the inspector lists and offers, and edits through the registry.
//   :hierarchy      HierarchyRow: the rows of the hierarchy panel, in sibling order, with the closed nodes' subtrees left out.
//   :editor_camera  EditorCamera: the viewport's camera (fly, orbit, pan, dolly, focus), never an entity.
//   :edits          The edits the editor makes (fields, components, scripts, names, hierarchy, subtrees) and applyEdit.
//   :undo_stack     UndoStack: the history of edits (merged drags, the saved marker).
//   :commands       The structural edits (create, destroy, duplicate, move, ...), applied and recorded.
//   :settled_extent SettledExtent: the viewport's render size, applied once the panel's size settles.
//   :play           PlaySession and copyWorld: play-in-editor's state and the play world's copy of the edit world.
//   :scene_file     Writing the edited scene to its file, and Save As's file names.
export module aegis.editor.core;

export import :asset_browser;
export import :chord;
export import :commands;
export import :component_text;
export import :console;
export import :context;
export import :edits;
export import :editor_camera;
export import :hierarchy;
export import :inspector;
export import :picking;
export import :play;
export import :preferences;
export import :property_model;
export import :scene_file;
export import :selection;
export import :settled_extent;
export import :undo_stack;
