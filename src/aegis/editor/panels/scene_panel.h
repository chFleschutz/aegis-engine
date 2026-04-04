#pragma once

#include "scene/entity.h"
#include "scene/components.h"

#include <imgui.h>

namespace Aegis::UI
{
	class ScenePanel
	{
	public:
		[[nodiscard]] auto selectedEntity() const -> Scene::Entity { return m_selectedEntity; }
		
		void draw();

	private:
		void drawHierachy();
		void drawAllEntities();
		void drawSceneSettings();
		void drawEntityProperties();

		void drawSingleEntity(Scene::Entity entity);
		auto drawEntityTreeNode(Scene::Entity entity, ImGuiTreeNodeFlags flags) -> bool;
		void drawEntityActions();
		void drawAddComponent();

		static void drawAssetSlot(const char* assetName, const char* description, bool assetSet = true);

		template<typename T>
			requires IsOptionalComponent<T>
		void drawComponent(const char* componentName, Scene::Entity entity, ImGuiTreeNodeFlags flags, 
			std::function<void(T&)> drawFunc)
		{
			if (!entity.has<T>())
				return;

			bool keepComponent = true;
			if (ImGui::CollapsingHeader(componentName, &keepComponent, flags))
				drawFunc(entity.get<T>());

			if (!keepComponent)
				entity.remove<T>();

			ImGui::Spacing();
			ImGui::Spacing();
		}

		template<typename T>
			requires IsRequiredComponent<T>
		void drawComponent(const char* componentName, Scene::Entity entity, ImGuiTreeNodeFlags flags,
			std::function<void(T&)> drawFunc)
		{
			AGX_ASSERT_X(entity.has<T>(), "Entity does not have the required component");

			if (ImGui::CollapsingHeader(componentName, nullptr, flags))
				drawFunc(entity.get<T>());

			ImGui::Spacing();
			ImGui::Spacing();
		}

		template<typename T>
		void drawAddComponentItem(const std::string& itemName)
		{
			if (m_selectedEntity.has<T>())
				return;

			if (ImGui::MenuItem(itemName.c_str()))
			{
				m_selectedEntity.add<T>();
				ImGui::CloseCurrentPopup();
			}
		}

		Scene::Entity m_selectedEntity{};
	};
}
