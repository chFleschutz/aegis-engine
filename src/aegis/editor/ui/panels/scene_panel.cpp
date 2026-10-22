module;

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

module aegis.editor;
import std;
import aegis.math;
import aegis.world;
import aegis.world.render_components;
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

// The hierarchy, all entities, the scene's singletons and the selected entity's properties. Edits apply
// directly: the panel runs at the frame's safe point.
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
        drawEntityProperties(editor, scene.registry());

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
            ImGui::DockBuilderDockWindow("Properties", dockBottomId);
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

    static void drawEntityProperties(EditorContext& editor, world::Registry& registry)
    {
        const auto selected = editor.selectedEntity();
        if (!ImGui::Begin("Properties") || !selected)
        {
            ImGui::End();
            return;
        }

        drawComponent<Name>(registry, "Name", selected, ImGuiTreeNodeFlags_DefaultOpen,
            [](Name& nameComponent) {
                ImGui::InputText("Entity Name", &nameComponent.name);
            });

        drawComponent<Transform>(registry, "Transform", selected, ImGuiTreeNodeFlags_DefaultOpen,
            [&](Transform& transform) {
                bool edited = ImGui::DragFloat3("Location", &transform.location.x, 0.1f);

                glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(transform.rotation));
                if (ImGui::DragFloat3("Rotation", &eulerDeg.x, 0.5f))
                {
                    transform.rotation = glm::quat(glm::radians(eulerDeg));
                    edited = true;
                }

                edited |= ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);

                // Static entities only move when told to (see DynamicTag)
                if (edited && !registry.has<DynamicTag>(selected))
                    registry.markStaticDirty(selected);
            });

        drawComponent<MeshRenderer>(registry, "Mesh Renderer", selected, ImGuiTreeNodeFlags_DefaultOpen,
            [](MeshRenderer& renderer) {
                drawAssetSlot("Mesh", "Mesh Asset", static_cast<bool>(renderer.mesh));
                drawAssetSlot("Material", "Material Asset", static_cast<bool>(renderer.material));
            });

        drawComponent<AmbientLight>(registry, "Ambient Light", selected, ImGuiTreeNodeFlags_DefaultOpen,
            [](AmbientLight& ambientLight) {
                ImGui::ColorEdit3("Color", &ambientLight.color.r);
                ImGui::DragFloat("Intensity", &ambientLight.intensity, 0.01f, 0.0f, 1.0f);
            });

        drawComponent<DirectionalLight>(registry, "Directional Light", selected, ImGuiTreeNodeFlags_DefaultOpen,
            [](DirectionalLight& directionalLight) {
                ImGui::ColorEdit3("Color", &directionalLight.color.r);
                ImGui::DragFloat("Intensity", &directionalLight.intensity, 0.1f, 0.0f, 10.0f);
            });

        drawComponent<PointLight>(registry, "Pointlight", selected, ImGuiTreeNodeFlags_DefaultOpen,
            [](PointLight& pointlight) {
                ImGui::ColorEdit3("Color", &pointlight.color.r);
                ImGui::DragFloat("Intensity", &pointlight.intensity, 0.1f, 0.0f, 1000.0f);
            });

        drawComponent<Camera>(registry, "Camera", selected, ImGuiTreeNodeFlags_DefaultOpen,
            [](Camera& camera) {
                float fovDeg = glm::degrees(camera.fov);
                ImGui::DragFloat("FOV", &fovDeg, 0.1f, 0.0f, 180.0f);
                camera.fov = glm::radians(fovDeg);

                ImGui::DragFloat("Near", &camera.near, 0.01f, 0.0f, 100.0f);
                ImGui::DragFloat("Far", &camera.far, 0.1f, 0.0f, 1000.0f);
                ImGui::Text("Aspect Ratio: %.2f", camera.aspect);
            });

        drawComponent<Parent>(registry, "Parent", selected, 0,
            [&](Parent& parent) {
                ImGui::Text("Parent: %s", parent.entity ? nameOf(registry, parent.entity) : "None");
            });

        drawComponent<Siblings>(registry, "Siblings", selected, 0,
            [&](Siblings& siblings) {
                ImGui::Text("Next: %s", siblings.next ? nameOf(registry, siblings.next) : "None");
                ImGui::Text("Prev: %s", siblings.prev ? nameOf(registry, siblings.prev) : "None");
            });

        drawComponent<Children>(registry, "Children", selected, 0,
            [&](Children& children) {
                ImGui::Text("Children: %d", static_cast<int>(children.count));
                ImGui::Text("First: %s", children.first ? nameOf(registry, children.first) : "None");
                ImGui::Text("Last: %s", children.last ? nameOf(registry, children.last) : "None");
            });

        drawAddComponent(registry, selected);
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

    static void drawAddComponent(world::Registry& registry, world::Entity entity)
    {
        ImGui::Spacing();

        const float addButtonWidth = 200.0f;
        const float windowWidth = ImGui::GetWindowWidth();
        ImGui::SetCursorPosX((windowWidth - addButtonWidth) * 0.5f);
        if (ImGui::Button("Add Component", ImVec2(addButtonWidth, 0.0f)))
            ImGui::OpenPopup("AddComponent");

        if (ImGui::BeginPopup("AddComponent"))
        {
            drawAddComponentItem<Name>(registry, entity, "Name");
            drawAddComponentItem<Transform>(registry, entity, "Transform");
            drawAddComponentItem<AmbientLight>(registry, entity, "Ambient Light");
            drawAddComponentItem<DirectionalLight>(registry, entity, "Directional Light");
            drawAddComponentItem<PointLight>(registry, entity, "Point Light");
            drawAddComponentItem<Camera>(registry, entity, "Camera");
            drawAddComponentItem<MeshRenderer>(registry, entity, "Mesh Renderer");
            ImGui::EndPopup();
        }
    }

    static void drawAssetSlot(const char* assetName, const char* description, bool assetSet = true)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
        if (!assetSet)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

        const ImVec2 buttonSize = ImVec2(50, 50);
        ImGui::Button(assetSet ? assetName : "None", buttonSize);

        if (!assetSet)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize.y - ImGui::GetTextLineHeightWithSpacing()) * 0.5f);
        ImGui::Text("%s", description);
        ImGui::PopStyleVar();
    }

    template<typename T>
        requires IsOptionalComponent<T>
    static void drawComponent(world::Registry& registry, const char* componentName, world::Entity entity,
        ImGuiTreeNodeFlags flags, const std::function<void(T&)>& drawFunc)
    {
        if (!registry.has<T>(entity))
            return;

        bool keepComponent = true;
        if (ImGui::CollapsingHeader(componentName, &keepComponent, flags))
            drawFunc(registry.get<T>(entity));

        if (!keepComponent)
            registry.remove<T>(entity);

        ImGui::Spacing();
        ImGui::Spacing();
    }

    template<typename T>
        requires IsRequiredComponent<T>
    static void drawComponent(world::Registry& registry, const char* componentName, world::Entity entity,
        ImGuiTreeNodeFlags flags, const std::function<void(T&)>& drawFunc)
    {
        if (!registry.has<T>(entity)) // Every entity has it (Registry::create)
            return;

        if (ImGui::CollapsingHeader(componentName, nullptr, flags))
            drawFunc(registry.get<T>(entity));

        ImGui::Spacing();
        ImGui::Spacing();
    }

    template<typename T>
    static void drawAddComponentItem(world::Registry& registry, world::Entity entity, const char* itemName)
    {
        if (registry.has<T>(entity))
            return;

        if (ImGui::MenuItem(itemName))
        {
            registry.add<T>(entity);
            ImGui::CloseCurrentPopup();
        }
    }
};
}

auto makeScenePanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<ScenePanel>();
}
}
