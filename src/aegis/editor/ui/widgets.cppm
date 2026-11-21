module aegis.editor:widgets;
import std;
import aegis.editor.core;
import aegis.engine;
import aegis.reflect;
import aegis.world;

// The widgets the panels share: hints, search fields, the icons of entities, components and assets, the Create menu
// and the asset drop target.
namespace aegis::editor
{
/// @brief Draws 'text' dimmed in the window's free space, centered line by line and wrapped inside a margin, for an empty panel. The
///        cursor stays where it was, so what follows (a drop target over the free space) still covers it.
void drawCenteredHint(std::string_view text);

/// @brief A text field with the search icon inside its left end and 'hint' while empty; returns what
///        ImGui::InputTextWithHint returns. 'flags' are ImGuiInputTextFlags.
auto searchField(const char* id, const char* hint, std::string& text, int flags = 0) -> bool;

/// @brief The icon of an entity of 'kind' (the hierarchy's rows, the inspector's header), and its color as an ImU32:
///        the viewport's for the cameras, the lights and the sky, the mesh's and the instance's own, the dimmed
///        text's for the rest.
[[nodiscard]] auto entityIcon(EntityKind kind) -> const char*;
[[nodiscard]] auto entityIconColor(EntityKind kind) -> std::uint32_t;

/// @brief The icon of the component or script 'type' (the inspector's sections, the Add menu).
[[nodiscard]] auto componentIcon(const reflect::TypeInfo& type) -> const char*;

/// @brief The icon of an asset of 'kind' (the asset list, the inspector's asset slots), and its color as an ImU32.
[[nodiscard]] auto assetIcon(AssetKind kind) -> const char*;
[[nodiscard]] auto assetIconColor(AssetKind kind) -> std::uint32_t;

/// @brief The Create menu's items, for a menu or a popup: an empty entity, then an entity of each registered component
///        type that can be created, by category, each a click away. Each creates one entity under 'parent' (a root entity
///        for a null one) as one undo entry, selects it and asks for its name to be typed (EditorContext::requestRename).
void drawCreateMenu(EditorContext& editor, FrameContext& frame, world::Entity parent = {});

/// @brief The asset dragged onto the item of the open drag and drop target, once dropped, if 'accepts' takes it; one it
///        refuses shows a tooltip saying so while it hovers. Call between BeginDragDropTarget and EndDragDropTarget.
[[nodiscard]] auto acceptAssetPayload(const std::function<bool(const AssetPayload& payload)>& accepts) -> std::optional<AssetPayload>;
}
