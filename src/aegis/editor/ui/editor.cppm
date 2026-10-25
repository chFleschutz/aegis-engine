// aegis.editor: the editor's ImGui layer, linked only into a game's <Game>-Editor (docs/editor.md).
//
// EditorShell is the engine's Shell for the editor flavor: it builds the editor's UI at the frame's safe
// point and keeps the world in edit mode. ImGui appears only in the .cpp files.
//
// Partitions:
//   :actions        ActionRegistry: the actions the menus and the shortcuts share.
//   :panel          Panel, one window (or group of windows) listed in the View menu.
//   :editor_shell   EditorShell.
//   :viewport       Viewport: the scene view with the editor camera, the gizmo and picking; EditorShell owns it.
//   :panels         (internal) The built-in panels, created by name.
export module aegis.editor;

export import aegis.editor.core;
export import :actions;
export import :editor_shell;
export import :panel;
export import :viewport;
