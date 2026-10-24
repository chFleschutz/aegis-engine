module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

module aegis.editor;
import std;
import aegis.core;
import aegis.math;
import aegis.world;
import :panels;

namespace aegis::editor
{
namespace
{
// The ImGui ID of an entity's row: its StableId, so an entity slot reused by another entity doesn't
// reopen the old tree state.
auto rowId(world::Registry& registry, world::Entity entity) -> const void*
{
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(registry.get<StableId>(entity).value));
}

auto nameOf(world::Registry& registry, world::Entity entity) -> const char*
{
    return registry.has<Name>(entity) ? registry.get<Name>(entity).name.c_str() : "Entity";
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
};

void applyDrop(const Drop& drop, EditorContext& editor, FrameContext& frame)
{
    auto& registry = editor.world()->registry();
    const auto entity = registry.find(drop.entity);
    const auto target = drop.target ? registry.find(*drop.target) : world::Entity{};
    if (!entity || (drop.target && !target))
        return;
    const auto edits = editWorld(editor, frame);
    const auto moved = drop.zone == DropZone::Into ? moveEntity(edits, editor.undoStack(), entity, target, std::numeric_limits<std::size_t>::max())
                                                   : moveEntityBeside(edits, editor.undoStack(), entity, target, drop.zone == DropZone::After);
    if (!moved)
        log::warn("Cannot move '{}': {}", nameOf(registry, entity), moved.error());
}

// The hierarchy, all entities and the scene's singletons, docked with the Inspector. Edits apply directly: the panel
// runs at the frame's safe point.
class ScenePanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Scene"; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        auto& scene = *editor.world();
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(400, 800));

        drawHierarchy(editor, frame);
        drawAllEntities(editor, frame);
        drawSceneSettings(editor, scene);

        const ImGuiID dockSpaceId = ImGui::GetID("SceneLayer");
        if (ImGui::DockBuilderGetNode(dockSpaceId) == nullptr)
        {
            ImGui::DockBuilderRemoveNode(dockSpaceId); // Clear out existing layout
            ImGui::DockBuilderAddNode(dockSpaceId);
            ImGui::DockBuilderSetNodePos(dockSpaceId, ImVec2(10, 30));
            ImGui::DockBuilderSetNodeSize(dockSpaceId, ImVec2(400, 600));

            ImGuiID dockMainId = dockSpaceId;
            const ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(dockSpaceId, ImGuiDir_Down, 0.50f, nullptr, &dockMainId);

            ImGui::DockBuilderDockWindow("Hierachy", dockMainId);
            ImGui::DockBuilderDockWindow("All Entities", dockMainId);
            ImGui::DockBuilderDockWindow("Scene Settings", dockMainId);
            ImGui::DockBuilderDockWindow("Inspector", dockBottomId); // The Inspector panel's
            ImGui::DockBuilderFinish(dockSpaceId);
        }
    }

private:
    static void drawHierarchy(EditorContext& editor, FrameContext& frame)
    {
        if (!ImGui::Begin("Hierachy"))
        {
            ImGui::End();
            return;
        }

        auto& registry = editor.world()->registry();
        constexpr std::uint8_t NODE_OPENED = 1 << 0;
        constexpr std::uint8_t VISITED = 1 << 1;
        std::vector<std::pair<world::Entity, std::uint8_t>> stack;
        std::optional<Drop> drop;

        auto view = registry.view<entt::entity>();
        stack.reserve(view.size());

        for (auto entt : view)
        {
            world::Entity entity{ entt };
            if (registry.has<Parent>(entity) && registry.get<Parent>(entity).entity)
                continue;

            stack.emplace_back(entity, 0);
        }

        while (!stack.empty())
        {
            auto& [entity, flags] = stack.back();

            if (flags & NODE_OPENED)
                ImGui::TreePop();

            if (flags & VISITED)
            {
                stack.pop_back();
                continue;
            }

            flags |= VISITED;

            auto& children = registry.get<Children>(entity);
            const ImGuiTreeNodeFlags treeNodeFlags = (children.count == 0) ? ImGuiTreeNodeFlags_Leaf : 0;
            if (drawEntityTreeNode(editor, registry, entity, treeNodeFlags, &drop))
            {
                flags |= NODE_OPENED;
                auto current = children.first;
                while (current)
                {
                    stack.emplace_back(current, 0);
                    current = registry.get<Siblings>(current).next;
                }
            }
        }

        // The space below the rows takes drops that make the entity a root
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload(entityPayload))
                drop = Drop{ .entity = *static_cast<const StableId*>(payload->Data) };
            ImGui::EndDragDropTarget();
        }
        if (drop)
            applyDrop(*drop, editor, frame);

        drawEntityActions(editor, frame);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            editor.selection().clear();

        ImGui::End();
    }

    static void drawAllEntities(EditorContext& editor, FrameContext& frame)
    {
        if (!ImGui::Begin("All Entities"))
        {
            ImGui::End();
            return;
        }

        auto& registry = editor.world()->registry();
        auto view = registry.view<entt::entity>();
        for (auto entt : view)
        {
            if (drawEntityTreeNode(editor, registry, world::Entity{ entt }, ImGuiTreeNodeFlags_Leaf))
                ImGui::TreePop();
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            editor.selection().clear();

        drawEntityActions(editor, frame);
        ImGui::End();
    }

    static void drawSceneSettings(EditorContext& editor, world::World& scene)
    {
        if (!ImGui::Begin("Scene Settings"))
        {
            ImGui::End();
            return;
        }

        auto& registry = scene.registry();
        drawSingleEntity(editor, registry, scene.mainCamera());
        drawSingleEntity(editor, registry, scene.ambientLight());
        drawSingleEntity(editor, registry, scene.directionalLight());

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            editor.selection().clear();

        ImGui::End();
    }

    static void drawSingleEntity(EditorContext& editor, world::Registry& registry, world::Entity entity)
    {
        if (!entity) // The scene's slot is empty (its entity was destroyed)
            return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;
        flags |= editor.isSelected(entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

        if (ImGui::TreeNodeEx(rowId(registry, entity), flags, "%s", nameOf(registry, entity)))
            ImGui::TreePop();

        if (ImGui::IsItemClicked())
            editor.select(entity);
    }

    // A row of the tree; with 'drop', it can be dragged onto another row and takes drops (see Drop).
    static auto drawEntityTreeNode(EditorContext& editor, world::Registry& registry, world::Entity entity, ImGuiTreeNodeFlags flags, std::optional<Drop>* drop = nullptr) -> bool
    {
        registry.getOrAdd<Children>(entity);

        flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= editor.isSelected(entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

        const bool isOpen = ImGui::TreeNodeEx(rowId(registry, entity), flags, "%s", nameOf(registry, entity));

        if (ImGui::IsItemClicked())
            editor.select(entity);

        if (drop)
            dragAndDrop(registry, entity, *drop);

        return isOpen;
    }

    // The row just drawn as a drag source and a drop target: the top and bottom quarters of the row place the
    // dragged entity before or after it, the middle makes it the row's last child.
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
        ImGui::EndDragDropTarget();
    }

    static void drawEntityActions(EditorContext& editor, FrameContext& frame)
    {
        // Right click on window to create entity
        if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("Create Entity"))
            {
                if (auto created = createEntity(editWorld(editor, frame), editor.undoStack(), "Empty Entity"))
                    editor.select(*created);
                else
                    log::warn("Cannot create an entity: {}", created.error());
            }

            const auto selected = editor.selectedEntity();
            if (selected && ImGui::MenuItem("Duplicate Selected Entity", "Ctrl+D"))
                duplicateSelected(editor, frame);
            if (selected && ImGui::MenuItem("Destroy Selected Entity"))
            {
                if (auto destroyed = destroyEntity(editWorld(editor, frame), editor.undoStack(), selected); !destroyed)
                    log::warn("Cannot destroy '{}': {}", nameOf(editor.world()->registry(), selected), destroyed.error());
                editor.selection().clear();
            }

            ImGui::EndPopup();
        }
    }
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

auto makeScenePanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<ScenePanel>();
}
}
