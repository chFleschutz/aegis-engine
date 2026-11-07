module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.core;
import aegis.math;
import aegis.serialize;
import aegis.world;
import aegis.world.render_components;
import :panels;
import :theme;

namespace aegis::editor
{
namespace
{
// The ImGui ID of an entity's row: its StableId, so an entity slot reused by another entity doesn't
// reopen the old tree state.
auto rowId(StableId id) -> const void*
{
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(id.value));
}

auto rowId(world::Registry& registry, world::Entity entity) -> const void*
{
    return rowId(registry.get<StableId>(entity));
}

auto nameOf(world::Registry& registry, world::Entity entity) -> const char*
{
    return registry.has<Name>(entity) ? registry.get<Name>(entity).name.c_str() : "Entity";
}

auto kindIcon(EntityKind kind) -> const char*
{
    switch (kind)
    {
    case EntityKind::Camera:
        return theme::icons::camera;
    case EntityKind::DirectionalLight:
        return theme::icons::sun;
    case EntityKind::PointLight:
        return theme::icons::light;
    case EntityKind::Mesh:
        return theme::icons::mesh;
    case EntityKind::SceneInstance:
        return theme::icons::instance;
    case EntityKind::Empty:
        return theme::icons::empty;
    }
    return theme::icons::empty;
}

// The icon's color: the viewport's for the cameras and lights, the dimmed text's for the rest.
auto kindColor(EntityKind kind) -> ImVec4
{
    switch (kind)
    {
    case EntityKind::Camera:
        return theme::cameraIcon;
    case EntityKind::DirectionalLight:
    case EntityKind::PointLight:
        return theme::lightIcon;
    default:
        return theme::textDim;
    }
}

// Whether the rows hold nothing but what a new scene starts with (the scene's camera and lights, a sky), or nothing at all.
auto holdsOnlyDefaults(world::World& scene, std::span<const HierarchyRow> rows) -> bool
{
    auto& registry = scene.registry();
    return std::ranges::all_of(rows, [&](const HierarchyRow& row) {
        const auto entity = row.entity;
        const bool isDefault = entity == scene.mainCamera() || entity == scene.ambientLight() || entity == scene.directionalLight() || registry.has<Sky>(entity);
        return isDefault && !row.hasChildren;
    });
}

// The drag and drop payload of a hierarchy row: the dragged entity's StableId.
constexpr const char* entityPayload = "AEGIS_ENTITY";

// Where a row was dropped on another: before or after it among its siblings, or into it as its last child.
enum class DropZone
{
    Before,
    Into,
    After,
};

// A drop to apply once the hierarchy is drawn: moving the entity while walking the tree would break the walk.
struct Drop
{
    StableId entity;
    std::optional<StableId> target; // None: the empty space below the rows, which makes the entity a root
    DropZone zone{ DropZone::Into };
    std::uint64_t asset{ 0 }; // A scene or model from the asset browser, instantiated into the target, instead of 'entity'
};

// A scene or model dragged from the asset browser, dropped this frame.
auto acceptInstance() -> std::uint64_t
{
    const auto dropped = acceptAssetPayload([](const AssetPayload& payload) { return canInstantiate(payload); });
    return dropped ? dropped->id : 0;
}

void applyDrop(const Drop& drop, EditorContext& editor, FrameContext& frame)
{
    auto& registry = editor.world()->registry();
    const auto target = drop.target ? registry.find(*drop.target) : world::Entity{};
    if (drop.target && !target)
        return;
    if (drop.asset)
    {
        instantiateAsset(editor, frame, drop.asset, target);
        return;
    }
    const auto dragged = registry.find(drop.entity);
    if (!dragged)
        return;
    // A selected row takes the other selected roots along, in their order; one that is not selected moves alone
    const auto entities = editor.isSelected(dragged) ? selectedRoots(registry, editor.selection()) : std::vector{ dragged };
    const auto edits = editWorld(editor, frame);
    auto& history = editor.undoStack();
    std::optional<UndoGroup> group;
    if (entities.size() > 1)
        group.emplace(history, std::format("Move {} entities", entities.size()));

    // Before or after the target the first one goes there, and each next one after the one before it
    world::Entity previous;
    for (const auto entity : entities)
    {
        std::expected<void, std::string> moved;
        if (drop.zone == DropZone::Into || !target)
        {
            if (entity == target)
                continue; // The row dropped into, also selected
            moved = moveEntity(edits, history, entity, target, std::numeric_limits<std::size_t>::max());
        }
        else if (previous)
            moved = moveEntityBeside(edits, history, entity, previous, true);
        else
            moved = moveEntityBeside(edits, history, entity, target, drop.zone == DropZone::After);
        if (moved)
            previous = entity;
        else
            log::warn("Cannot move '{}': {}", nameOf(registry, entity), moved.error());
    }
}

// The hierarchy: a filter and a Create button above the tree. Edits apply directly: the panel runs at the frame's safe
// point. A context menu opens after the rows are drawn, so the action it runs cannot change the world under the rows.
class ScenePanel final : public Panel
{
public:
    explicit ScenePanel(const ActionRegistry& actions) :
        m_actions{ actions }
    {
        setOpen(true);
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "Scene"; }

    void draw(EditorContext& editor, FrameContext& frame) override { drawHierarchy(editor, frame); }

private:
    void drawHierarchy(EditorContext& editor, FrameContext& frame)
    {
        if (!ImGui::Begin("Hierarchy"))
        {
            ImGui::End();
            return;
        }

        drawToolbar(editor, frame);

        auto& registry = editor.world()->registry();
        std::optional<Drop> drop;
        std::optional<StableId> menuRow; // The row right-clicked this frame

        const bool filtered = !m_filter.empty();
        followSelection(editor, registry, filtered);
        const auto& rows = cachedRows(registry);
        m_rowClicked = false;
        const auto scrollTo = m_scrollTo ? rowIndex(rows, *m_scrollTo) : std::nullopt;
        m_scrollTo.reset();

        // Only the rows in view are drawn. The rows are flat, each indented by its depth rather than nested in its parent's
        // tree node, so any of them can be skipped
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        if (scrollTo)
            clipper.IncludeItemByIndex(static_cast<int>(*scrollTo));
        while (clipper.Step())
        {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            {
                const auto& row = rows[static_cast<std::size_t>(index)];
                const float indent = static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing;
                if (row.depth > 0)
                    ImGui::Indent(indent);
                if (row.hasChildren)
                    ImGui::SetNextItemOpen(row.open);
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | (row.hasChildren ? 0 : ImGuiTreeNodeFlags_Leaf);
                const bool open = drawEntityTreeNode(editor, registry, rows, row.entity, flags, drop, menuRow);
                if (scrollTo == static_cast<std::size_t>(index))
                    ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleCenterY);
                if (row.depth > 0)
                    ImGui::Unindent(indent);

                // Opened or closed by a click: the rows follow from the next frame
                if (row.hasChildren && open != row.open)
                    setNodeOpen(row.id, open, filtered);
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_narrowOnRelease.reset(); // Released over no row, or the row is gone
        if (filtered && rows.empty())
            ImGui::TextDisabled("No entity matches");

        if (filtered && rows.empty())
            drawCenteredHint("No matching entities");
        else if (!filtered && holdsOnlyDefaults(*editor.world(), rows))
            drawCenteredHint("Right-click to create an entity, or drop a model here");

        // The space below the rows takes drops that make the entity a root
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload(entityPayload))
                drop = Drop{ .entity = *static_cast<const StableId*>(payload->Data) };
            else if (const auto asset = acceptInstance())
                drop = Drop{ .asset = asset };
            ImGui::EndDragDropTarget();
        }
        if (drop)
            applyDrop(*drop, editor, frame);

        if (menuRow)
        {
            m_menuRow = menuRow;
            ImGui::OpenPopup("RowMenu");
        }
        drawRowMenu(editor, frame);
        drawWindowMenu(editor, frame);

        // A click on the empty space clears the selection. A row clicked with Ctrl or Shift held doesn't become the active
        // item (ImGui's tree nodes take no click with a modifier), so the window still counts as hovered then
        if (!m_rowClicked && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            editor.selection().clear();
        m_followed = editor.selection().active(); // What the rows did to the selection needs no following

        ImGui::End();
    }

    // The rows to draw, rebuilt only when the hierarchy (Registry::hierarchyVersion, which also tells another world), the
    // open nodes or the filter changed. While filtering, the nodes keep a tree state of their own: closing one there
    // doesn't close it in the full tree.
    auto cachedRows(world::Registry& registry) -> const std::vector<HierarchyRow>&
    {
        const auto version = registry.hierarchyVersion();
        if (!m_rowsDirty && m_rowsVersion == version && m_rowsFilter == m_filter)
            return m_rows;
        if (m_filter.empty())
            m_rows = hierarchyRows(registry, [this](StableId id) { return m_openNodes.contains(id.value); });
        else
            m_rows = closeRows(hierarchyRows(registry, {}, m_filter), [this](StableId id) { return !m_closedWhileFiltered.contains(id.value); });
        m_rowsVersion = version;
        m_rowsFilter = m_filter;
        m_rowsDirty = false;
        return m_rows;
    }

    // The full tree remembers the open nodes, the filtered rows the closed ones.
    void setNodeOpen(StableId id, bool open, bool filtered)
    {
        auto& nodes = filtered ? m_closedWhileFiltered : m_openNodes;
        if (open != filtered)
            nodes.insert(id.value);
        else
            nodes.erase(id.value);
        m_rowsDirty = true;
    }

    // A selection made elsewhere (a pick in the viewport, a shortcut, an undo) opens the nodes above its active entity and
    // scrolls to its row. While filtering, a row that the filter hides stays hidden.
    void followSelection(EditorContext& editor, world::Registry& registry, bool filtered)
    {
        const auto active = editor.selection().active();
        if (active == m_followed)
            return;
        m_followed = active;
        const auto entity = active ? registry.find(*active) : world::Entity{};
        if (!entity)
            return;
        for (const auto ancestor : ancestorIds(registry, entity))
        {
            if (filtered)
                m_rowsDirty |= m_closedWhileFiltered.erase(ancestor.value) > 0;
            else
                m_rowsDirty |= m_openNodes.insert(ancestor.value).second;
        }
        m_scrollTo = active;
    }

    // The filter and the button that opens the Create menu.
    void drawToolbar(EditorContext& editor, FrameContext& frame)
    {
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputTextWithHint("##filter", "Filter", &m_filter);
        ImGui::SameLine();
        if (ImGui::Button("+", ImVec2{ button, button }))
            ImGui::OpenPopup("CreateMenu");
        ImGui::SetItemTooltip("Create an entity");
        if (ImGui::BeginPopup("CreateMenu"))
        {
            drawCreateMenu(editor, frame);
            ImGui::EndPopup();
        }
    }

    // The menu of the row that was right-clicked (selected by the click, so the actions, which act on the selection, act on
    // it and on the other selected rows).
    void drawRowMenu(EditorContext& editor, FrameContext& frame)
    {
        if (!ImGui::BeginPopup("RowMenu"))
            return;
        auto& registry = editor.world()->registry();
        const auto row = m_menuRow ? registry.find(*m_menuRow) : world::Entity{};
        if (!row)
        {
            ImGui::CloseCurrentPopup(); // It is gone (an undo, a scene change)
            ImGui::EndPopup();
            return;
        }

        m_actions.menuItem("edit.rename", frame);
        m_actions.menuItem("edit.duplicate", frame);
        m_actions.menuItem("edit.delete", frame);
        ImGui::Separator();
        if (ImGui::BeginMenu("Create Child", !registry.has<serialize::InstanceMember>(row)))
        {
            drawCreateMenu(editor, frame, row);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        m_actions.menuItem("edit.focus", frame);
        ImGui::EndPopup();
    }

    // The menu of the empty space and of the window's edge, not of the rows (their own opens there).
    void drawWindowMenu(EditorContext& editor, FrameContext& frame)
    {
        if (!ImGui::BeginPopupContextWindow("WindowMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            return;
        if (ImGui::BeginMenu("Create"))
        {
            drawCreateMenu(editor, frame);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // The kind's icon and the name on the row just drawn, which has no label of its own. The members of a scene instance
    // are dimmed: the Inspector shows them read-only.
    static void drawRowLabel(world::Registry& registry, world::Entity entity)
    {
        const auto kind = entityKind(registry, entity);
        const bool member = registry.has<serialize::InstanceMember>(entity);
        const ImVec4 iconColor = member ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kindColor(kind);
        const ImU32 textColor = ImGui::GetColorU32(member ? ImGuiCol_TextDisabled : ImGuiCol_Text);

        const ImVec2 min = ImGui::GetItemRectMin();
        // Centered on the row: a tree node has no vertical padding of its own
        const ImVec2 position{ min.x + ImGui::GetTreeNodeToLabelSpacing(), min.y + (ImGui::GetItemRectSize().y - ImGui::GetTextLineHeight()) * 0.5f };
        const char* icon = kindIcon(kind);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(position, ImGui::ColorConvertFloat4ToU32(iconColor), icon);
        drawList->AddText(ImVec2{ position.x + ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemInnerSpacing.x, position.y }, textColor, nameOf(registry, entity));
    }

    // A row of the tree: it can be dragged onto another row and takes drops (see Drop). A click selects it (see select), a double
    // click on its label frames the selection in the viewport (the arrow opens it), a right click asks for its menu ('menuRow').
    auto drawEntityTreeNode(EditorContext& editor, world::Registry& registry, std::span<const HierarchyRow> rows, world::Entity entity, ImGuiTreeNodeFlags flags,
        std::optional<Drop>& drop, std::optional<StableId>& menuRow) -> bool
    {
        flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= editor.isSelected(entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

        // The label is drawn by hand: the kind's icon comes first, in its own color
        const bool isOpen = ImGui::TreeNodeEx(rowId(registry, entity), flags, "");
        drawRowLabel(registry, entity);

        select(editor.selection(), rows, registry.get<StableId>(entity));
        const bool onLabel = ImGui::GetMousePos().x >= ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
        if (onLabel && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            editor.requestFocus();
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            menuRow = registry.get<StableId>(entity);

        dragAndDrop(editor, registry, entity, drop);

        return isOpen;
    }

    // The clicks on the row just drawn, the row of 'id': a click selects it alone, Ctrl+click adds or removes it,
    // Shift+click selects the rows from the active one to it. A click on one of several selected rows selects it alone
    // only when the button is released without a drag, so a drag moves them all. A right click on a selected row keeps
    // the selection (its menu acts on all of it) and makes the row the active one.
    void select(Selection& selection, std::span<const HierarchyRow> rows, StableId id)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_rowClicked = true;
            if (io.KeyCtrl)
                selection.toggle(id);
            else if (io.KeyShift)
                selection.assign(rangeSelection(rows, selection.active().value_or(id), id));
            else if (selection.contains(id) && selection.ids().size() > 1)
                m_narrowOnRelease = id;
            else
                selection.select(id);
        }
        if (m_narrowOnRelease == id && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (ImGui::IsItemHovered() && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
                selection.select(id);
            m_narrowOnRelease.reset();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (selection.contains(id))
                selection.add(id);
            else
                selection.select(id);
        }
    }

    // The row just drawn as a drag source and a drop target: the top and bottom quarters of the row place the
    // dragged entity before or after it, the middle makes it the row's last child. A selected row drags the other
    // selected roots along (see applyDrop). A scene or model dropped from the asset browser is instantiated as its last
    // child.
    static void dragAndDrop(EditorContext& editor, world::Registry& registry, world::Entity entity, std::optional<Drop>& drop)
    {
        const auto id = registry.get<StableId>(entity);
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(entityPayload, &id, sizeof(id));
            const auto count = editor.isSelected(entity) ? selectedRoots(registry, editor.selection()).size() : 1;
            if (count > 1)
                ImGui::Text("%s and %zu more", nameOf(registry, entity), count - 1);
            else
                ImGui::TextUnformatted(nameOf(registry, entity));
            ImGui::EndDragDropSource();
        }

        if (!ImGui::BeginDragDropTarget())
            return;
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float y = (ImGui::GetMousePos().y - min.y) / std::max(max.y - min.y, 1.0f);
        const DropZone zone = y < 0.25f ? DropZone::Before : (y > 0.75f ? DropZone::After : DropZone::Into);
        const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery | (zone == DropZone::Into ? 0 : ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (const auto* payload = ImGui::AcceptDragDropPayload(entityPayload, flags))
        {
            if (zone != DropZone::Into)
            {
                const float lineY = zone == DropZone::Before ? min.y : max.y;
                ImGui::GetWindowDrawList()->AddLine(ImVec2{ min.x, lineY }, ImVec2{ max.x, lineY }, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f * theme::scale());
            }
            if (payload->IsDelivery())
                drop = Drop{ .entity = *static_cast<const StableId*>(payload->Data), .target = id, .zone = zone };
        }
        else if (const auto asset = acceptInstance()) // Instantiated as the row's last child, wherever it is dropped
            drop = Drop{ .target = id, .asset = asset };
        ImGui::EndDragDropTarget();
    }

    const ActionRegistry& m_actions;
    std::string m_filter;
    std::vector<HierarchyRow> m_rows;                        // The rows drawn, see cachedRows
    std::uint64_t m_rowsVersion{ 0 };                        // The hierarchy version m_rows were built at
    std::string m_rowsFilter;                                // And the filter
    bool m_rowsDirty{ true };                                // A node opened or closed since
    std::unordered_set<std::uint64_t> m_openNodes;           // By StableId; the others are closed
    std::unordered_set<std::uint64_t> m_closedWhileFiltered; // By StableId: closed in the filtered rows, which open the others
    std::optional<StableId> m_followed;                      // The active entity the rows last showed, see followSelection
    std::optional<StableId> m_scrollTo;                      // The row to scroll to this frame
    std::optional<StableId> m_menuRow;                       // The row the open context menu is for
    std::optional<StableId> m_narrowOnRelease;               // One of several selected rows, pressed: selected alone if released without a drag
    bool m_rowClicked{ false };                              // A row took this frame's left click
};
}

auto duplicateSelected(EditorContext& editor, FrameContext& frame) -> bool
{
    if (!editor.world())
        return false;
    auto& registry = editor.world()->registry();
    const auto roots = selectedRoots(registry, editor.selection());
    if (roots.empty())
        return false;
    auto& history = editor.undoStack();
    std::optional<UndoGroup> group;
    if (roots.size() > 1)
        group.emplace(history, std::format("Duplicate {} entities", roots.size()));

    std::vector<StableId> copies;
    for (const auto root : roots)
    {
        if (auto copy = duplicateEntity(editWorld(editor, frame), history, root))
            copies.push_back(registry.get<StableId>(*copy));
        else
            log::warn("Cannot duplicate '{}': {}", nameOf(registry, root), copy.error());
    }
    if (copies.empty())
        return false;
    editor.selection().assign(copies);
    return true;
}

auto destroySelected(EditorContext& editor, FrameContext& frame) -> bool
{
    if (!editor.world())
        return false;
    auto& registry = editor.world()->registry();
    const auto roots = selectedRoots(registry, editor.selection());
    if (roots.empty())
        return false;
    auto& history = editor.undoStack();
    std::optional<UndoGroup> group;
    if (roots.size() > 1)
        group.emplace(history, std::format("Delete {} entities", roots.size()));

    bool destroyed = false;
    for (const auto root : roots)
    {
        const std::string name = nameOf(registry, root);
        if (auto result = destroyEntity(editWorld(editor, frame), history, root))
            destroyed = true;
        else
            log::warn("Cannot destroy '{}': {}", name, result.error());
    }
    editor.selection().clear();
    return destroyed;
}

auto makeScenePanel(const ActionRegistry& actions) -> std::unique_ptr<Panel>
{
    return std::make_unique<ScenePanel>(actions);
}
}
