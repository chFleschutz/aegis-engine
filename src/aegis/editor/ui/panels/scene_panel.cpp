module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

module aegis.editor;
import std;
import aegis.core;
import aegis.math;
import aegis.serialize;
import aegis.world;
import aegis.world.render_components;
import :panels;

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
    const auto entity = registry.find(drop.entity);
    if (!entity)
        return;
    const auto edits = editWorld(editor, frame);
    const auto moved = drop.zone == DropZone::Into ? moveEntity(edits, editor.undoStack(), entity, target, std::numeric_limits<std::size_t>::max())
                                                   : moveEntityBeside(edits, editor.undoStack(), entity, target, drop.zone == DropZone::After);
    if (!moved)
        log::warn("Cannot move '{}': {}", nameOf(registry, entity), moved.error());
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

        // While filtering, the rows are all open and keep a tree state of their own: closing one there doesn't close it in the full tree
        const bool filtered = !m_filter.empty();
        if (filtered)
            ImGui::PushID("filtered");

        // A node is open when ImGui's tree state says so: the rows then follow it in the same frame
        const auto rows = hierarchyRows(registry, [](StableId id) { return ImGui::GetStateStorage()->GetInt(ImGui::GetID(rowId(id)), 0) != 0; }, m_filter);

        // A node that was just closed by a click still has its children in 'rows': they are skipped
        std::optional<std::uint32_t> closedDepth;
        for (const auto& row : rows)
        {
            if (closedDepth && row.depth > *closedDepth)
                continue;
            closedDepth.reset();

            const float indent = static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing;
            if (row.depth > 0)
                ImGui::Indent(indent);
            const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_NoTreePushOnOpen | (row.hasChildren ? (filtered ? ImGuiTreeNodeFlags_DefaultOpen : 0) : ImGuiTreeNodeFlags_Leaf);
            const bool open = drawEntityTreeNode(editor, registry, row.entity, flags, drop, menuRow);
            if (row.depth > 0)
                ImGui::Unindent(indent);

            if (row.hasChildren && !open)
                closedDepth = row.depth;
        }
        if (filtered)
        {
            ImGui::PopID();
            if (rows.empty())
                ImGui::TextDisabled("No entity matches");
        }

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

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            editor.selection().clear();

        ImGui::End();
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

    // The menu of the row that was right-clicked (already selected, so the actions, which act on the selection, act on it).
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

    // A row of the tree: it can be dragged onto another row and takes drops (see Drop). A click selects it, a double click on its label
    // frames it in the viewport (the arrow opens it), a right click selects it and asks for its menu ('menuRow').
    static auto drawEntityTreeNode(EditorContext& editor, world::Registry& registry, world::Entity entity, ImGuiTreeNodeFlags flags, std::optional<Drop>& drop, std::optional<StableId>& menuRow) -> bool
    {
        flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= editor.isSelected(entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

        const bool isOpen = ImGui::TreeNodeEx(rowId(registry, entity), flags, "%s", nameOf(registry, entity));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
            editor.select(entity);
        const bool onLabel = ImGui::GetMousePos().x >= ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
        if (onLabel && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            editor.requestFocus();
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            menuRow = registry.get<StableId>(entity);

        dragAndDrop(registry, entity, drop);

        return isOpen;
    }

    // The row just drawn as a drag source and a drop target: the top and bottom quarters of the row place the
    // dragged entity before or after it, the middle makes it the row's last child. A scene or model dropped from the
    // asset browser is instantiated as its last child.
    static void dragAndDrop(world::Registry& registry, world::Entity entity, std::optional<Drop>& drop)
    {
        const auto id = registry.get<StableId>(entity);
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(entityPayload, &id, sizeof(id));
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
                ImGui::GetWindowDrawList()->AddLine(ImVec2{ min.x, lineY }, ImVec2{ max.x, lineY }, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
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
    std::optional<StableId> m_menuRow; // The row the open context menu is for
};
}

auto duplicateSelected(EditorContext& editor, FrameContext& frame) -> bool
{
    const auto selected = editor.selectedEntity();
    if (!selected)
        return false;
    auto copy = duplicateEntity(editWorld(editor, frame), editor.undoStack(), selected);
    if (!copy)
    {
        log::warn("Cannot duplicate '{}': {}", nameOf(editor.world()->registry(), selected), copy.error());
        return false;
    }
    editor.select(*copy);
    return true;
}

auto destroySelected(EditorContext& editor, FrameContext& frame) -> bool
{
    const auto selected = editor.selectedEntity();
    if (!selected)
        return false;
    const bool destroyed = [&] {
        auto result = destroyEntity(editWorld(editor, frame), editor.undoStack(), selected);
        if (!result)
            log::warn("Cannot destroy '{}': {}", nameOf(editor.world()->registry(), selected), result.error());
        return result.has_value();
    }();
    editor.selection().clear();
    return destroyed;
}

auto makeScenePanel(const ActionRegistry& actions) -> std::unique_ptr<Panel>
{
    return std::make_unique<ScenePanel>(actions);
}
}
