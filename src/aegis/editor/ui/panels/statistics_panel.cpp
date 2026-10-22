module;

#include <imgui.h>

module aegis.editor;
import std;
import aegis.render_graph;
import aegis.world;
import aegis.world.render_components;
import :panels;

namespace aegis::editor
{
namespace
{
// Counts of the world's entities and components, and of the instances the renderer draws.
class StatisticsPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Statistics"; }

    void draw(EditorContext&, FrameContext& frame) override
    {
        auto& registry = frame.world.registry();
        const auto& instances = frame.instances;

        ImGui::Begin("Scene Statistics");
        ImGui::Text("Entities: %d", static_cast<int>(registry.entityCount()));
        ImGui::Text(" - Dynamic Entities: %d", static_cast<int>(registry.view<DynamicTag>().size()));
        ImGui::Separator();
        ImGui::Text("Mesh renderers: %d", static_cast<int>(registry.view<MeshRenderer>().size()));
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

auto makeStatisticsPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<StatisticsPanel>();
}
}
