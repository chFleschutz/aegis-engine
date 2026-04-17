module;

#include <imgui.h>

export module Aegis.Editor.Panels:StatisticsPanel;

import Aegis.Scene.Registry;
import Aegis.Graphics.Components;
import Aegis.Graphics.DrawBatchRegistry;

export namespace Aegis::Editor
{
	class StatisticsPanel
	{
	public:
		void draw(Scene::Registry& registry, Graphics::DrawBatchRegistry& drawBatcher)
		{
			ImGui::Begin("Scene Statistics");
			ImGui::Text("Entities: %d", static_cast<int>(registry.entityCount()));
			ImGui::Text(" - Dynamic Entities: %d", static_cast<int>(registry.view<DynamicTag>().size()));
			ImGui::Separator();

			ImGui::Text("Meshes: %d", static_cast<int>(registry.view<Graphics::Mesh>().size()));
			ImGui::Text("Materials: %d", static_cast<int>(registry.view<Graphics::Material>().size()));
			ImGui::Text("Point Lights: %d", static_cast<int>(registry.view<PointLight>().size()));
			ImGui::Text("Cameras: %d", static_cast<int>(registry.view<Camera>().size()));
			ImGui::Separator();

			ImGui::Text("Draw Batches: %d", drawBatcher.batchCount());
			ImGui::Text("Total Instances: %d", drawBatcher.instanceCount());
			ImGui::Text(" - Static Instances: %d", drawBatcher.staticInstanceCount());
			ImGui::Text(" - Dynamic Instances: %d", drawBatcher.dynamicInstanceCount());

			ImGui::End();
		}
	};
}