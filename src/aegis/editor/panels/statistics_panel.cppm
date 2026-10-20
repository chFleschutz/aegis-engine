module;

#include <imgui.h>

export module aegis.editor.panels:statistics_panel;

import aegis.scene.registry;
import aegis.assets;
import aegis.scene.render_components;
import aegis.render_graph;

export namespace aegis::editor
{
class StatisticsPanel
{
public:
    void draw(scene::Registry& registry, const aegis::render_graph::DrawBatchRegistry& drawBatches)
    {
        ImGui::Begin("Scene Statistics");
        ImGui::Text("Entities: %d", static_cast<int>(registry.entityCount()));
        ImGui::Text(" - Dynamic Entities: %d", static_cast<int>(registry.view<DynamicTag>().size()));
        ImGui::Separator();
        ImGui::Text("Meshes: %d", static_cast<int>(registry.view<aegis::Mesh>().size()));
        ImGui::Text("Materials: %d", static_cast<int>(registry.view<aegis::Material>().size()));
        ImGui::Text("Point Lights: %d", static_cast<int>(registry.view<PointLight>().size()));
        ImGui::Text("Cameras: %d", static_cast<int>(registry.view<Camera>().size()));
        ImGui::Separator();
        ImGui::Text("Draw Batches: %d", static_cast<int>(drawBatches.batchCount()));
        ImGui::Text("Total Instances: %d", static_cast<int>(drawBatches.instanceCount()));
        ImGui::Text(" - Static Instances: %d", static_cast<int>(drawBatches.staticInstanceCount()));
        ImGui::Text(" - Dynamic Instances: %d", static_cast<int>(drawBatches.dynamicInstanceCount()));
        ImGui::End();
    }
};
}
