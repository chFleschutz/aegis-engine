// aegis.editor.core: the editor's model, with no ImGui and no GPU (docs/editor.md).
//
// The ImGui layer (aegis.editor) reads and changes the world through it; everything here is testable on a
// plain world (aegis-tests-editor).
//
// Partitions:
//   :console     ConsoleBuffer: the log lines the console shows, with filters.
//   :preferences EditorPreferences: the user's settings, kept in the config folder.
//   :selection   The selected entities, by StableId.
//   :context     EditorContext: the edited world and the selection, shared by the panels.
export module aegis.editor.core;

export import :console;
export import :context;
export import :preferences;
export import :selection;
