module aegis.editor:widgets;
import std;
import aegis.assets;
import aegis.editor.core;
import aegis.engine;
import aegis.reflect;
import aegis.world;

// The widgets the panels and the bars share: the bars' buttons and tabular digits, hints, search fields, the icons of entities, components and assets, the Create menu
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

/// @brief The registered fields of 'object' (an instance of 'type') as a two-column property grid, one row per field
///        (propertyRows): the inspector's sections and the renderer panel's pass settings. 'edit' gets each field edited
///        this frame, its new value and whether its widget is still active (being dragged or typed in); 'released', if
///        set, is called when a widget is let go. 'readOnly' disables every row; 'database' names the assets of asset slots.
void drawPropertyGrid(const reflect::TypeInfo& type, const void* object, bool readOnly, const assets::AssetDatabase& database,
    const std::function<void(const PropertyRow& row, const reflect::Value& value, bool active)>& edit,
    const std::function<void()>& released = {});

/// @brief The widget of the property grid's row 'row' (of a field of 'type') showing 'value': for a grid of the panel's own
///        (the Material panel's). The new value when it was edited this frame; ImGui's last item is the widget.
[[nodiscard]] auto drawPropertyValue(const reflect::TypeInfo& type, const PropertyRow& row, const reflect::Value& value,
    const assets::AssetDatabase& database, bool editable) -> std::optional<reflect::Value>;

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

/// @brief A button in the menu bar, the status bar or a top bar: shorter than a frame by an inset above and below, its
///        label padded within. It aligns to the bar's text, which a bar puts its padding down (AlignTextToFramePadding),
///        so it sits centered in the bar.
auto barButton(const char* label) -> bool;

/// @brief A part of a bar button's label: text (or an icon) in its own color (an ImU32), 'gap' after the part before it.
struct LabelPart
{
    std::string text;
    std::uint32_t color;
    float gap;
};

/// @brief A bar button (barButton's metrics) whose label is 'parts', each in its color; 'lit' fills it with the accent,
///        as a drawer button while its panel shows. 'id' is unique among the window's items.
auto labelButton(const char* id, std::span<const LabelPart> parts, bool lit) -> bool;

/// @brief Text whose digits all take the widest digit's width, each centered in it: the UI font's figures are
///        proportional, so a number that changes every frame would change the text's width and move what is laid out
///        around it.
class TabularText
{
public:
    TabularText();

    [[nodiscard]] auto width(std::string_view text) const -> float;
    /// @brief Draws 'text' in 'color' (an ImU32) into the window's draw list, from the screen position 'x', 'y'.
    void draw(float x, float y, std::uint32_t color, std::string_view text) const;

private:
    float m_digit{ 0.0f };
};
}
