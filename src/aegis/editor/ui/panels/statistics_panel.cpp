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
    [[nodiscard]] auto debug() const -> bool override { return true; }
    [[nodiscard]] auto drawer() const -> bool override { return true; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        auto& registry = editor.world()->registry();
        const auto& instances = frame.instances;
        const auto summary = statisticsSummary(editor, frame);

        ImGui::Text("Entities: %d", static_cast<int>(summary.entities));
        ImGui::Text(" - Dynamic Entities: %d", static_cast<int>(registry.view<DynamicTag>().size()));
        ImGui::Separator();
        ImGui::Text("Mesh renderers: %d", static_cast<int>(registry.view<MeshRenderer>().size()));
        ImGui::Text("Point Lights: %d", static_cast<int>(registry.view<PointLight>().size()));
        ImGui::Text("Cameras: %d", static_cast<int>(registry.view<Camera>().size()));
        ImGui::Separator();
        ImGui::Text("Draw Batches: %d", static_cast<int>(summary.drawBatches));
        ImGui::Text("Total Instances: %d", static_cast<int>(summary.instances));
        ImGui::Text(" - Static Instances: %d", static_cast<int>(instances.staticInstanceCount()));
        ImGui::Text(" - Dynamic Instances: %d", static_cast<int>(instances.dynamicInstanceCount()));
    }
};
}

auto statisticsSummary(const EditorContext& editor, const FrameContext& frame) -> StatisticsSummary
{
    auto* world = editor.world();
    if (!world)
        return {};
    return {
        .entities = world->registry().entityCount(),
        .drawBatches = frame.instances.batchCount(),
        .instances = frame.instances.instanceCount(),
    };
}

auto makeStatisticsPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<StatisticsPanel>();
}
}
