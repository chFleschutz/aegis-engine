#include "pch.h"
#include "statistics_panel.h"

#include "engine.h"
#include "scene/components.h"

namespace Aegis::UI
{
	void StatisticsPanel::draw()
	{
		auto& registry = Engine::scene().registry();

		ImGui::Begin("Scene Statistics");
		ImGui::Text("Entities: %d", registry.storage<entt::entity>().size());
		ImGui::Text(" - Dynamic Entities: %d", registry.view<DynamicTag>().size());
		ImGui::Separator();

		ImGui::Text("Meshes: %d", registry.view<Mesh>().size());
		ImGui::Text("Materials: %d", registry.view<Material>().size());
		ImGui::Text("Point Lights: %d", registry.view<PointLight>().size());
		ImGui::Text("Cameras: %d", registry.view<Camera>().size());
		ImGui::Separator();

		auto& drawBatcher = Engine::renderer().drawBatchRegistry();
		ImGui::Text("Draw Batches: %d", drawBatcher.batchCount());
		ImGui::Text("Total Instances: %d", drawBatcher.instanceCount());
		ImGui::Text(" - Static Instances: %d", drawBatcher.staticInstanceCount());
		ImGui::Text(" - Dynamic Instances: %d", drawBatcher.dynamicInstanceCount());

		ImGui::End();
	}
}