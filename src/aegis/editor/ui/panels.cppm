module aegis.editor:panels;
import std;
import aegis.math;
import aegis.reflect;
import aegis.world;
import :actions;
import :panel;

namespace aegis::editor
{
// The built-in panels, in their menus' order; each is defined in its own .cpp (ui/panels/). The preferences window is
// not listed in a menu: Edit > Preferences... opens it.
// The hierarchy; its menus show the actions' labels and shortcuts, so 'actions' must outlive it.
[[nodiscard]] auto makeScenePanel(const ActionRegistry& actions) -> std::unique_ptr<Panel>;
[[nodiscard]] auto makeAllEntitiesPanel() -> std::unique_ptr<Panel>;
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

/// @brief Draws 'text' dimmed in the window's free space, centered line by line and wrapped inside a margin, for an empty panel. The
///        cursor stays where it was, so what follows (a drop target over the free space) still covers it.
void drawCenteredHint(std::string_view text);

/// @brief The items of a menu of 'types', one submenu per category (those without one at the top); the type picked this frame, if any.
[[nodiscard]] auto pickType(std::span<const reflect::TypeInfo* const> types) -> const reflect::TypeInfo*;

/// @brief The Create menu's items, for a menu or a popup: an empty entity, a camera, a point light, a directional light,
///        then the registered component types by category. Each creates one entity under 'parent' (a root entity for a
///        null one) as one undo entry, selects it and asks for its name to be typed (EditorContext::requestRename).
void drawCreateMenu(EditorContext& editor, FrameContext& frame, world::Entity parent = {});

/// @brief The asset dragged onto the item of the open drag and drop target, once dropped, if 'accepts' takes it; one it
///        refuses shows a tooltip saying so while it hovers. Call between BeginDragDropTarget and EndDragDropTarget.
[[nodiscard]] auto acceptAssetPayload(const std::function<bool(const AssetPayload& payload)>& accepts) -> std::optional<AssetPayload>;

/// @brief Instantiates the scene or model 'asset' under 'parent' (a root for a null one), at 'location' if given,
///        undoably, and selects it; false if it failed (logged).
auto instantiateAsset(EditorContext& editor, FrameContext& frame, std::uint64_t asset, world::Entity parent, std::optional<glm::vec3> location = std::nullopt) -> bool;
}
