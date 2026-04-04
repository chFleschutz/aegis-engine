#include "pch.h"
#include "scene_panel.h"

#include "engine.h"
#include "scene/components.h"

#include <glm/gtc/type_ptr.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <ImGuizmo.h>

namespace Aegis::UI
{
	void ScenePanel::draw()
	{
		ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(400, 800));

		drawHierachy();
		drawAllEntities();
		drawSceneSettings();
		drawEntityProperties();

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

	void ScenePanel::drawHierachy()
	{
		if (!ImGui::Begin("Hierachy"))
		{
			ImGui::End();
			return;
		}

		constexpr uint8_t NODE_OPENED = 1 << 0;
		constexpr uint8_t VISITED = 1 << 1;
		std::vector<std::pair<Scene::Entity, uint8_t>> stack;

		auto& scene = Engine::instance().scene();
		auto view = scene.registry().view<entt::entity>();
		stack.reserve(view.size());

		for (auto entt : view)
		{
			Scene::Entity entity{ entt, &scene };
			if (entity.has<Parent>() && entity.get<Parent>().entity)
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

			auto& children = entity.get<Children>();
			ImGuiTreeNodeFlags treeNodeflags = (children.count == 0) ? ImGuiTreeNodeFlags_Leaf : 0;
			if (drawEntityTreeNode(entity, treeNodeflags))
			{
				flags |= NODE_OPENED;
				for (auto it = children.rbegin(); it != children.rend(); ++it)
				{
					stack.emplace_back(*it, 0);
				}
			}
		}

		drawEntityActions();

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered())
			m_selectedEntity = {};

		ImGui::End();
	}

	void ScenePanel::drawAllEntities()
	{
		if (!ImGui::Begin("All Entities"))
		{
			ImGui::End();
			return;
		}

		auto& scene = Engine::instance().scene();
		auto view = scene.registry().view<entt::entity>();
		for (auto entt : view)
		{
			if (drawEntityTreeNode(Scene::Entity{ entt, &scene }, ImGuiTreeNodeFlags_Leaf))
				ImGui::TreePop();
		}

		if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
			m_selectedEntity = {};

		drawEntityActions();
		ImGui::End();
	}

	void ScenePanel::drawSceneSettings()
	{
		if (!ImGui::Begin("Scene Settings"))
		{
			ImGui::End();
			return;
		}

		auto& scene = Engine::instance().scene();
		drawSingleEntity(scene.mainCamera());
		drawSingleEntity(scene.ambientLight());
		drawSingleEntity(scene.directionalLight());

		if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
			m_selectedEntity = {};

		ImGui::End();
	}

	void ScenePanel::drawEntityProperties()
	{
		if (!ImGui::Begin("Properties") || !m_selectedEntity)
		{
			ImGui::End();
			return;
		}

		drawComponent<Name>("Name", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
			[](Name& nameComponent)
			{
				ImGui::InputText("Entity Name", &nameComponent.name);
			});

		drawComponent<Transform>("Transform", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
			[](Transform& transform)
			{
				ImGui::DragFloat3("Location", &transform.location.x, 0.1f);

				glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(transform.rotation));
				if (ImGui::DragFloat3("Rotation", &eulerDeg.x, 0.5f))
					transform.rotation = glm::quat(glm::radians(eulerDeg));

				ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
			});

		drawComponent<Mesh>("Mesh", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
			[](Mesh& mesh)
			{
				drawAssetSlot("Mesh", "Mesh Asset", mesh.staticMesh != nullptr);
			});

		drawComponent<AmbientLight>("Ambient Light", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
			[](AmbientLight& ambientLight)
			{
				ImGui::ColorEdit3("Color", &ambientLight.color.r);
				ImGui::DragFloat("Intensity", &ambientLight.intensity, 0.01f, 0.0f, 1.0f);
			});

		drawComponent<DirectionalLight>("Directional Light", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
			[](DirectionalLight& directionalLight)
			{
				ImGui::ColorEdit3("Color", &directionalLight.color.r);
				ImGui::DragFloat("Intensity", &directionalLight.intensity, 0.1f, 0.0f, 10.0f);
			});

		drawComponent<PointLight>("Pointlight", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
			[](PointLight& pointlight)
			{
				ImGui::ColorEdit3("Color", &pointlight.color.r);
				ImGui::DragFloat("Intensity", &pointlight.intensity, 0.1f, 0.0f, 1000.0f);
			});

		drawComponent<Camera>("Camera", m_selectedEntity, ImGuiTreeNodeFlags_DefaultOpen,
			[](Camera& camera)
			{
				float fovDeg = glm::degrees(camera.fov);
				ImGui::DragFloat("FOV", &fovDeg, 0.1f, 0.0f, 180.0f);
				camera.fov = glm::radians(fovDeg);

				ImGui::DragFloat("Near", &camera.near, 0.01f, 0.0f, 100.0f);
				ImGui::DragFloat("Far", &camera.far, 0.1f, 0.0f, 1000.0f);
				ImGui::Text("Aspect Ratio: %.2f", camera.aspect);
			});

		drawComponent<Parent>("Parent", m_selectedEntity, 0,
			[](Parent& parent)
			{
				ImGui::Text("Parent: %s", parent.entity ? parent.entity.get<Name>().name.c_str() : "None");
			});

		drawComponent<Siblings>("Siblings", m_selectedEntity, 0,
			[](Siblings& siblings)
			{
				ImGui::Text("Next: %s", siblings.next ? siblings.next.get<Name>().name.c_str() : "None");
				ImGui::Text("Prev: %s", siblings.prev ? siblings.prev.get<Name>().name.c_str() : "None");
			});

		drawComponent<Children>("Children", m_selectedEntity, 0,
			[](Children& children)
			{
				ImGui::Text("Children: %d", children.count);
				ImGui::Text("First: %s", children.first ? children.first.get<Name>().name.c_str() : "None");
				ImGui::Text("Last: %s", children.last ? children.last.get<Name>().name.c_str() : "None");
			});

		drawAddComponent();

		ImGui::End();
	}

	void ScenePanel::drawSingleEntity(Scene::Entity entity)
	{
		auto name = entity.has<Name>() ? entity.get<Name>().name.c_str() : "Entity";
		auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;
		flags |= (m_selectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

		if (ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, name))
			ImGui::TreePop();

		if (ImGui::IsItemClicked())
			m_selectedEntity = entity;
	}

	auto ScenePanel::drawEntityTreeNode(Scene::Entity entity, ImGuiTreeNodeFlags flags) -> bool
	{
		auto& children = entity.getOrAdd<Children>();

		auto name = entity.has<Name>() ? entity.get<Name>().name.c_str() : "Entity";
		flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
		flags |= (m_selectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0; // Highlight selection

		auto isOpen = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, name);

		if (ImGui::IsItemClicked())
			m_selectedEntity = entity;

		return isOpen;
	}

	void ScenePanel::drawEntityActions()
	{
		// Right click on window to create entity
		if (ImGui::BeginPopupContextWindow(0, ImGuiPopupFlags_MouseButtonRight))
		{
			if (ImGui::MenuItem("Create Entity"))
				m_selectedEntity = Engine::instance().scene().createEntity("Empty Entity");

			if (m_selectedEntity && ImGui::MenuItem("Destroy Selected Entity"))
			{
				Engine::instance().scene().destroyEntity(m_selectedEntity);
				m_selectedEntity = {};
			}

			ImGui::EndPopup();
		}
	}

	void ScenePanel::drawAddComponent()
	{
		ImGui::Spacing();

		float addButtonWidth = 200.0f;
		float windowWidth = ImGui::GetWindowWidth();
		ImGui::SetCursorPosX((windowWidth - addButtonWidth) * 0.5f);
		if (ImGui::Button("Add Component", ImVec2(addButtonWidth, 0.0f)))
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
			drawAddComponentItem<Name>("Name");
			drawAddComponentItem<Transform>("Transform");
			drawAddComponentItem<AmbientLight>("Ambient Light");
			drawAddComponentItem<DirectionalLight>("Directional Light");
			drawAddComponentItem<PointLight>("Point Light");
			drawAddComponentItem<Camera>("Camera");
			drawAddComponentItem<Mesh>("Mesh");
			drawAddComponentItem<Material>("Default Material");
			ImGui::EndPopup();
		}
	}

	void ScenePanel::drawAssetSlot(const char* buttonLabel, const char* description, bool assetSet)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
		if (!assetSet)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

		ImVec2 buttonSize = ImVec2(50, 50);
		if (ImGui::Button(assetSet ? buttonLabel : "None", buttonSize))
		{
		}

		if (!assetSet)
			ImGui::PopStyleColor();

		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize.y - ImGui::GetTextLineHeightWithSpacing()) * 0.5f);
		ImGui::Text(description);
		ImGui::PopStyleVar();
	}
}
