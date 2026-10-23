module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

module aegis.editor;
import std;
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

// The hierarchy, all entities and the scene's singletons, docked with the Inspector. Edits apply directly: the panel
// runs at the frame's safe point.
class ScenePanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Scene"; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        auto& scene = frame.world;
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(400, 800));

        drawHierarchy(editor, scene.registry());
        drawAllEntities(editor, scene.registry());
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
    static void drawHierarchy(EditorContext& editor, world::Registry& registry)
    {
        if (!ImGui::Begin("Hierachy"))
        {
            ImGui::End();
            return;
        }

        constexpr std::uint8_t NODE_OPENED = 1 << 0;
        constexpr std::uint8_t VISITED = 1 << 1;
        std::vector<std::pair<world::Entity, std::uint8_t>> stack;

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
            if (drawEntityTreeNode(editor, registry, entity, treeNodeFlags))
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

        drawEntityActions(editor, registry);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            editor.selection().clear();

        ImGui::End();
    }

    static void drawAllEntities(EditorContext& editor, world::Registry& registry)
    {
        if (!ImGui::Begin("All Entities"))
        {
            ImGui::End();
            return;
        }

        auto view = registry.view<entt::entity>();
        for (auto entt : view)
        {
            if (drawEntityTreeNode(editor, registry, world::Entity{ entt }, ImGuiTreeNodeFlags_Leaf))
                ImGui::TreePop();
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
            editor.selection().clear();

        drawEntityActions(editor, registry);
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

    static auto drawEntityTreeNode(EditorContext& editor, world::Registry& registry, world::Entity entity, ImGuiTreeNodeFlags flags) -> bool
    {
        registry.getOrAdd<Children>(entity);

        flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= editor.isSelected(entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

        const bool isOpen = ImGui::TreeNodeEx(rowId(registry, entity), flags, "%s", nameOf(registry, entity));

        if (ImGui::IsItemClicked())
            editor.select(entity);

        return isOpen;
    }

    static void drawEntityActions(EditorContext& editor, world::Registry& registry)
    {
        // Right click on window to create entity
        if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("Create Entity"))
                editor.select(registry.create("Empty Entity"));

            const auto selected = editor.selectedEntity();
            if (selected && ImGui::MenuItem("Destroy Selected Entity"))
            {
                registry.destroy(selected);
                editor.selection().clear();
            }

            ImGui::EndPopup();
        }
    }
};
}

auto makeScenePanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<ScenePanel>();
}
}
