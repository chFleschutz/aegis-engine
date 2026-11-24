module aegis.editor;
import std;
import aegis.core;
import aegis.math;
import aegis.render_graph;
import aegis.world;
import :statistics;

namespace aegis::editor
{
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
        const auto location = registry.get<GlobalTransform>(*source).location();
        const auto copy = registry.create("Statistics Check", location, math::Quat::identity(), math::Float3{ 0.0f });
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
}
