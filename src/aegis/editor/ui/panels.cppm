module aegis.editor:panels;
import std;
import :panel;

namespace aegis::editor
{
// The built-in panels, in the View menu's order; each is defined in its own .cpp (ui/panels/).
[[nodiscard]] auto makeScenePanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeInspectorPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeConsolePanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makePreferencesPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeRendererPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeStatisticsPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeProfilerPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeDemoPanel() -> std::unique_ptr<Panel>;
}

namespace aegis::editor
{
/// @brief The frame's world as the edits see it; the asset library resolves the references of written components.
[[nodiscard]] auto editWorld(FrameContext& frame) -> EditWorld;

/// @brief Steps the edit history back (or forward) and writes the value to the world; false if there is nothing to do or
///        the edit no longer applies (logged).
auto undoEdit(EditorContext& editor, FrameContext& frame) -> bool;
auto redoEdit(EditorContext& editor, FrameContext& frame) -> bool;
}
