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
			ImGui::Text("Entities: %d", registry.entityCount());
			ImGui::Text(" - Dynamic Entities: %d", registry.view<DynamicTag>().size());
			ImGui::Separator();

			ImGui::Text("Meshes: %d", registry.view<Graphics::Mesh>().size());
			ImGui::Text("Materials: %d", registry.view<Graphics::Material>().size());
			ImGui::Text("Point Lights: %d", registry.view<PointLight>().size());
			ImGui::Text("Cameras: %d", registry.view<Camera>().size());
			ImGui::Separator();

			ImGui::Text("Draw Batches: %d", drawBatcher.batchCount());
			ImGui::Text("Total Instances: %d", drawBatcher.instanceCount());
			ImGui::Text(" - Static Instances: %d", drawBatcher.staticInstanceCount());
			ImGui::Text(" - Dynamic Instances: %d", drawBatcher.dynamicInstanceCount());

			ImGui::End();
		}
	};
}