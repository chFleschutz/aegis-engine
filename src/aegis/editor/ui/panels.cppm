module aegis.editor:panels;
import std;
import aegis.math;
import aegis.world;
import :panel;

namespace aegis::editor
{
// The built-in panels, in the View menu's order; each is defined in its own .cpp (ui/panels/).
[[nodiscard]] auto makeScenePanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeInspectorPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeAssetPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeConsolePanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makePreferencesPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeRendererPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeStatisticsPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeProfilerPanel() -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeDemoPanel() -> std::unique_ptr<Panel>;
}

namespace aegis::editor
{
/// @brief The world the panels show (the edit world, or the play world while playing) as the edits see it; the asset library resolves the references of written components.
[[nodiscard]] auto editWorld(const EditorContext& editor, FrameContext& frame) -> EditWorld;

/// @brief Steps the edit history back (or forward) and writes the value to the world; false if there is nothing to do or
///        the edit no longer applies (logged).
auto undoEdit(EditorContext& editor, FrameContext& frame) -> bool;
auto redoEdit(EditorContext& editor, FrameContext& frame) -> bool;

/// @brief Duplicates the selected entity and selects the copy; false if nothing is selected or it failed (logged).
auto duplicateSelected(EditorContext& editor, FrameContext& frame) -> bool;

/// @brief Destroys the selected entity and its descendants, undoably, and clears the selection; false if nothing is
///        selected or it failed (logged).
auto destroySelected(EditorContext& editor, FrameContext& frame) -> bool;

/// @brief The asset dragged onto the item of the open drag and drop target, once dropped, if 'accepts' takes it; one it
///        refuses shows a tooltip saying so while it hovers. Call between BeginDragDropTarget and EndDragDropTarget.
[[nodiscard]] auto acceptAssetPayload(const std::function<bool(const AssetPayload& payload)>& accepts) -> std::optional<AssetPayload>;

/// @brief Instantiates the scene or model 'asset' under 'parent' (a root for a null one), at 'location' if given,
///        undoably, and selects it; false if it failed (logged).
auto instantiateAsset(EditorContext& editor, FrameContext& frame, std::uint64_t asset, world::Entity parent, std::optional<glm::vec3> location = std::nullopt) -> bool;
}
