module;

#include <imgui.h>

module aegis.editor;
import std;
import aegis.core;
import aegis.math;
import aegis.render_graph;
import aegis.world;
import :panels;
import :theme;

namespace aegis::editor
{
namespace
{
// Counts of the world's entities and components, and of the instances the renderer draws.
class StatisticsPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Statistics"; }
    [[nodiscard]] auto icon() const -> const char* override { return theme::icons::stats; }
    [[nodiscard]] auto debug() const -> bool override { return true; }
    [[nodiscard]] auto drawer() const -> bool override { return true; }

    void draw(EditorContext& editor, FrameContext& frame) override
    {
        auto& registry = editor.world()->registry();
        const auto& instances = frame.instances(*editor.world());
        const auto summary = statisticsSummary(editor, frame);

        ImGui::Text("Entities: %d", static_cast<int>(summary.entities));
        ImGui::Text(" - Dynamic Entities: %d", static_cast<int>(registry.view<DynamicTag>().size()));
        ImGui::Separator();
        ImGui::Text("Mesh renderers: %d", static_cast<int>(registry.view<MeshRenderer>().size()));
        ImGui::Text("Point Lights: %d", static_cast<int>(registry.view<PointLight>().size()));
        ImGui::Text("Spot Lights: %d", static_cast<int>(registry.view<SpotLight>().size()));
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
        .drawBatches = frame.instances(*world).batchCount(),
        .instances = frame.instances(*world).instanceCount(),
    };
}

void StatisticsCheck::onFrame(const EditorContext& editor, FrameContext& frame)
{
    auto* play = editor.world();
    if (m_step == Step::Done || !editor.playing() || !play)
        return;
    auto& registry = play->registry();
    if (m_step == Step::Add)
    {
        m_step = Step::Done; // Unless a copy is added
        std::optional<world::Entity> source;
        for (const auto handle : registry.view<MeshRenderer, GlobalTransform>())
            source = world::Entity{ handle };
        if (!source)
        {
            log::error("Editor statistics check failed: the scene has no mesh to copy");
            return;
        }
        const auto renderer = registry.get<MeshRenderer>(*source);
        const auto location = registry.get<GlobalTransform>(*source).location;
        const auto copy = registry.create("Statistics Check", location, glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f });
        registry.add<MeshRenderer>(copy, renderer);
        m_step = Step::Verify;
        m_waitFrames = 2;
        return;
    }
    if (m_waitFrames-- > 0)
        return;
    m_step = Step::Done;
    const auto summary = statisticsSummary(editor, frame);
    const auto& played = frame.instances(*play);
    const auto& edited = frame.instances(frame.world);
    const auto editEntities = frame.world.registry().entityCount();
    std::vector<std::string> problems;
    if (summary.instances != played.instanceCount() || summary.instances != edited.instanceCount() + 1)
        problems.push_back(std::format("{} instances, the play world has {}, the edit world {} (+1 expected)", summary.instances, played.instanceCount(), edited.instanceCount()));
    if (summary.drawBatches != played.batchCount())
        problems.push_back(std::format("{} draw batches, the play world has {}", summary.drawBatches, played.batchCount()));
    if (summary.entities != registry.entityCount() || summary.entities != editEntities + 1)
        problems.push_back(std::format("{} entities, the play world has {}, the edit world {} (+1 expected)", summary.entities, registry.entityCount(), editEntities));
    if (problems.empty())
    {
        log::info("Editor statistics verified: during play they count the play world's {} entities, {} instances and {} draw batches",
            summary.entities, summary.instances, summary.drawBatches);
        return;
    }
    for (const auto& problem : problems)
        log::error("Editor statistics check failed: the statistics show {}", problem);
}

void StatisticsCheck::onShutdown() const
{
    if (m_step != Step::Done)
        log::error("Editor statistics check failed: the run ended before it was checked (no play?)");
}

auto makeStatisticsPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<StatisticsPanel>();
}
}
