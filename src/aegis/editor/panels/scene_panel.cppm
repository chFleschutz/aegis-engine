module;

#include "core/assert.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <memory>

export module Aegis.Editor.Panels:ScenePanel;

import Aegis.Scene;
import Aegis.Scene.Entity;
import Aegis.Scene.Components;
import Aegis.Graphics.Components;

export namespace Aegis::Editor
{
	class ScenePanel
	{
	public:
		[[nodiscard]] auto selectedEntity() const -> Scene::Entity { return m_selectedEntity; }

		void draw(Scene::Scene& scene)
		{
			ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(400, 800));

			drawHierachy(scene);
			drawAllEntities(scene.registry());
			drawSceneSettings(scene);
			drawEntityProperties(scene.registry());

			ImGuiID dockSpaceID = ImGui::GetID("SceneLayer");
			if (ImGui::DockBuilderGetNode(dockSpaceID) == NULL)
			{
				ImGui::DockBuilderRemoveNode(dockSpaceID); // Clear out existing layout
				ImGui::DockBuilderAddNode(dockSpaceID);
				ImGui::DockBuilderSetNodePos(dockSpaceID, ImVec2(10, 30));
				ImGui::DockBuilderSetNodeSize(dockSpaceID, ImVec2(400, 600));

				ImGuiID dockMainID = dockSpaceID;
				ImGuiID dockBottomID = ImGui::DockBuilderSplitNode(dockSpaceID, ImGuiDir_Down, 0.50f, NULL, &dockMainID);

				ImGui::DockBuilderDockWindow("Hierachy", dockMainID);
				ImGui::DockBuilderDockWindow("All Entities", dockMainID);
				ImGui::DockBuilderDockWindow("Scene Settings", dockMainID);
				ImGui::DockBuilderDockWindow("Properties", dockBottomID);
				ImGui::DockBuilderFinish(dockSpaceID);
			}
		}

	private:
		void drawHierachy(Scene::Scene& scene)
		{
			if (!ImGui::Begin("Hierachy"))
			{
				ImGui::End();
				return;
			}

			constexpr uint8_t NODE_OPENED = 1 << 0;
			constexpr uint8_t VISITED = 1 << 1;
			std::vector<std::pair<Scene::Entity, uint8_t>> stack;

			auto& registry = scene.registry();
			auto view = registry.view<entt::entity>();
			stack.reserve(view.size());

			for (auto entt : view)
			{
				Scene::Entity entity{ entt };
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
				ImGuiTreeNodeFlags treeNodeflags = (children.count == 0) ? ImGuiTreeNodeFlags_Leaf : 0;
				if (drawEntityTreeNode(registry, entity, treeNodeflags))
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

			drawEntityActions(registry);

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
				m_selectedEntity = {};

			ImGui::End();
		}

		void drawAllEntities(Scene::Registry& registry)
		{
			if (!ImGui::Begin("All Entities"))
			{
				ImGui::End();
				return;
			}

			auto view = registry.view<entt::entity>();
			for (auto entt : view)
			{
				if (drawEntityTreeNode(registry, Scene::Entity{ entt }, ImGuiTreeNodeFlags_Leaf))
					ImGui::TreePop();
			}

			if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
				m_selectedEntity = {};

			drawEntityActions(registry);
			ImGui::End();
		}

		void drawSceneSettings(Scene::Scene& scene)
		{
			if (!ImGui::Begin("Scene Settings"))
			{
				ImGui::End();
				return;
			}

			auto& registry = scene.registry();
			drawSingleEntity(registry, scene.mainCamera());
			drawSingleEntity(registry, scene.ambientLight());
			drawSingleEntity(registry, scene.directionalLight());

			if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
				m_selectedEntity = {};

			ImGui::End();
		}

		void drawEntityProperties(Scene::Registry& registry)
		{
			if (!ImGui::Begin("Properties") || !m_selectedEntity)
			{
				ImGui::End();
				return;
			}

			drawComponent<Name>(registry, "Name", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
				[](Name& nameComponent)
				{
					ImGui::InputText("Entity Name", &nameComponent.name);
				});

			drawComponent<Transform>(registry, "Transform", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
				[](Transform& transform)
				{
					ImGui::DragFloat3("Location", &transform.location.x, 0.1f);

					glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(transform.rotation));
					if (ImGui::DragFloat3("Rotation", &eulerDeg.x, 0.5f))
						transform.rotation = glm::quat(glm::radians(eulerDeg));

					ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
				});

			drawComponent<Graphics::Mesh>(registry, "Mesh", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
				[](Graphics::Mesh& mesh)
				{
					drawAssetSlot("Mesh", "Mesh Asset", mesh.staticMesh != nullptr);
				});

			drawComponent<AmbientLight>(registry, "Ambient Light", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
				[](AmbientLight& ambientLight)
				{
					ImGui::ColorEdit3("Color", &ambientLight.color.r);
					ImGui::DragFloat("Intensity", &ambientLight.intensity, 0.01f, 0.0f, 1.0f);
				});

			drawComponent<DirectionalLight>(registry, "Directional Light", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
				[](DirectionalLight& directionalLight)
				{
					ImGui::ColorEdit3("Color", &directionalLight.color.r);
					ImGui::DragFloat("Intensity", &directionalLight.intensity, 0.1f, 0.0f, 10.0f);
				});

			drawComponent<PointLight>(registry, "Pointlight", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
				[](PointLight& pointlight)
				{
					ImGui::ColorEdit3("Color", &pointlight.color.r);
					ImGui::DragFloat("Intensity", &pointlight.intensity, 0.1f, 0.0f, 1000.0f);
				});

			drawComponent<Camera>(registry, "Camera", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
				[](Camera& camera)
				{
					float fovDeg = glm::degrees(camera.fov);
					ImGui::DragFloat("FOV", &fovDeg, 0.1f, 0.0f, 180.0f);
					camera.fov = glm::radians(fovDeg);

					ImGui::DragFloat("Near", &camera.near, 0.01f, 0.0f, 100.0f);
					ImGui::DragFloat("Far", &camera.far, 0.1f, 0.0f, 1000.0f);
					ImGui::Text("Aspect Ratio: %.2f", camera.aspect);
				});

			drawComponent<Parent>(registry, "Parent", m_selectedEntity, 0,
				[&](Parent& parent)
				{
					ImGui::Text("Parent: %s", parent.entity ? registry.get<Name>(parent.entity).name.c_str() : "None");
				});

			drawComponent<Siblings>(registry, "Siblings", m_selectedEntity, 0,
				[&](Siblings& siblings)
				{
					ImGui::Text("Next: %s", siblings.next ? registry.get<Name>(siblings.next).name.c_str() : "None");
					ImGui::Text("Prev: %s", siblings.prev ? registry.get<Name>(siblings.prev).name.c_str() : "None");
				});

			drawComponent<Children>(registry, "Children", m_selectedEntity, 0,
				[&](Children& children)
				{
					ImGui::Text("Children: %d", static_cast<int>(children.count));
					ImGui::Text("First: %s", children.first ? registry.get<Name>(children.first).name.c_str() : "None");
					ImGui::Text("Last: %s", children.last ? registry.get<Name>(children.last).name.c_str() : "None");
				});

			drawAddComponent(registry);
			ImGui::End();
		}

		void drawSingleEntity(Scene::Registry& registry, Scene::Entity entity)
		{
			auto name = registry.has<Name>(entity) ? registry.get<Name>(entity).name.c_str() : "Entity";
			auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;
			flags |= (m_selectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

			if (ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", name))
				ImGui::TreePop();

			if (ImGui::IsItemClicked())
				m_selectedEntity = entity;
		}

		auto drawEntityTreeNode(Scene::Registry& registry, Scene::Entity entity, ImGuiTreeNodeFlags flags) -> bool
		{
			auto& children = registry.getOrAdd<Children>(entity);

			auto name = registry.has<Name>(entity) ? registry.get<Name>(entity).name.c_str() : "Entity";
			flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
			flags |= (m_selectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

			auto isOpen = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, "%s", name);

			if (ImGui::IsItemClicked())
				m_selectedEntity = entity;

			return isOpen;
		}

		void drawEntityActions(Scene::Registry& registry)
		{
			// Right click on window to create entity
			if (ImGui::BeginPopupContextWindow(0, ImGuiPopupFlags_MouseButtonRight))
			{
				if (ImGui::MenuItem("Create Entity"))
					m_selectedEntity = registry.create("Empty Entity");

				if (m_selectedEntity && ImGui::MenuItem("Destroy Selected Entity"))
				{
					registry.destroy(m_selectedEntity);
					m_selectedEntity = {};
				}

				ImGui::EndPopup();
			}
		}

		void drawAddComponent(Scene::Registry& registry)
		{
			ImGui::Spacing();

			float addButtonWidth = 200.0f;
			float windowWidth = ImGui::GetWindowWidth();
			ImGui::SetCursorPosX((windowWidth - addButtonWidth) * 0.5f);
			if (ImGui::Button("Add Component", ImVec2(addButtonWidth, 0.0f)))
				ImGui::OpenPopup("AddComponent");

			if (ImGui::BeginPopup("AddComponent"))
			{
				drawAddComponentItem<Name>(registry, "Name");
				drawAddComponentItem<Transform>(registry, "Transform");
				drawAddComponentItem<AmbientLight>(registry, "Ambient Light");
				drawAddComponentItem<DirectionalLight>(registry, "Directional Light");
				drawAddComponentItem<PointLight>(registry, "Point Light");
				drawAddComponentItem<Camera>(registry, "Camera");
				drawAddComponentItem<Graphics::Mesh>(registry, "Mesh");
				drawAddComponentItem<Graphics::Material>(registry, "Default Material");
				ImGui::EndPopup();
			}
		}

		static void drawAssetSlot(const char* assetName, const char* description, bool assetSet = true)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
			if (!assetSet)
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

			ImVec2 buttonSize = ImVec2(50, 50);
			if (ImGui::Button(assetSet ? assetName : "None", buttonSize))
			{
			}

			if (!assetSet)
				ImGui::PopStyleColor();

			ImGui::SameLine();
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize.y - ImGui::GetTextLineHeightWithSpacing()) * 0.5f);
			ImGui::Text("%s", description);
			ImGui::PopStyleVar();
		}

		template<typename T>
			requires IsOptionalComponent<T>
		void drawComponent(Scene::Registry& registry, const char* componentName, Scene::Entity entity, ImGuiTreeNodeFlags flags,
			std::function<void(T&)> drawFunc)
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
		void drawComponent(Scene::Registry& registry, const char* componentName, Scene::Entity entity, ImGuiTreeNodeFlags flags,
			std::function<void(T&)> drawFunc)
		{
			AGX_ASSERT_X(registry.has<T>(entity), "Entity does not have the required component");

			if (ImGui::CollapsingHeader(componentName, nullptr, flags))
				drawFunc(registry.get<T>(entity));

			ImGui::Spacing();
			ImGui::Spacing();
		}

		template<typename T>
		void drawAddComponentItem(Scene::Registry& registry, const std::string& itemName)
		{
			if (registry.has<T>(m_selectedEntity))
				return;

			if (ImGui::MenuItem(itemName.c_str()))
			{
				registry.add<T>(m_selectedEntity);
				ImGui::CloseCurrentPopup();
			}
		}

		Scene::Entity m_selectedEntity{};
	};
}
