module;

#include <imgui.h>

export module aegis.editor.panels:statistics_panel;

import aegis.world.registry;
import aegis.assets;
import aegis.world.render_components;
import aegis.render_graph;

export namespace aegis::editor
{
class StatisticsPanel
{
public:
    void draw(world::Registry& registry, const aegis::render_graph::InstanceStore& instances)
    {
        ImGui::Begin("Scene Statistics");
        ImGui::Text("Entities: %d", static_cast<int>(registry.entityCount()));
        ImGui::Text(" - Dynamic Entities: %d", static_cast<int>(registry.view<DynamicTag>().size()));
        ImGui::Separator();
        ImGui::Text("Mesh renderers: %d", static_cast<int>(registry.view<aegis::MeshRenderer>().size()));
        ImGui::Text("Point Lights: %d", static_cast<int>(registry.view<PointLight>().size()));
        ImGui::Text("Cameras: %d", static_cast<int>(registry.view<Camera>().size()));
        ImGui::Separator();
        ImGui::Text("Draw Batches: %d", static_cast<int>(instances.batchCount()));
        ImGui::Text("Total Instances: %d", static_cast<int>(instances.instanceCount()));
        ImGui::Text(" - Static Instances: %d", static_cast<int>(instances.staticInstanceCount()));
        ImGui::Text(" - Dynamic Instances: %d", static_cast<int>(instances.dynamicInstanceCount()));
        ImGui::End();
    }
};
}
