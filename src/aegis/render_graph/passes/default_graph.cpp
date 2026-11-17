module aegis.render_passes;

namespace aegis::render_graph
{
auto addSharedPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> const InstanceUpdatePass*
{
    if (!desc.gpuDriven)
        return nullptr;
    return &graph.add<InstanceUpdatePass>(desc.scene);
}

auto addScenePasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    const auto& scene = desc.scene;
    graph.add<SceneUpdatePass>(scene, desc.view);
    if (desc.gpuDriven)
    {
        const auto& instances = desc.instances ? *desc.instances : graph.add<InstanceUpdatePass>(scene);
        graph.add<ClearDrawCountsPass>();
        graph.add<CullingPass>(scene, instances);
        graph.add<GpuDrivenGeometryPass>(scene, instances);
    }
    else
    {
        graph.add<GeometryPass>(scene).addRenderSystem<StaticMeshRenderSystem>();
    }

    graph.add<LightingPass>(scene, desc.view);
    graph.add<SkyBoxPass>(scene, desc.view);
    graph.add<TransparentPass>(scene).addRenderSystem<PointLightRenderSystem>();
    graph.add<BloomPass>(scene);
    graph.add<PostProcessingPass>(scene);
}

auto addOutputPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    std::vector<std::string> finals;
    for (const auto& view : desc.views)
        finals.push_back(scopedName(view.name, names::finalImage));

    if (desc.shownViews)
    {
        // The frame's target shows only the UI; each "Final" stays its view's, for the UI to show.
        graph.add<UiPass>(UiPass::Desc{ .context = desc.context, .targetFormat = desc.shownViews->targetFormat, .finals = std::move(finals) });
        return;
    }
    if (desc.ui)
        graph.add<UiPass>(UiPass::Desc{ .context = desc.context, .finals = { finals.front() } });
    graph.add<PresentPass>(finals.front());
}

auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    auto viewDesc = desc;
    viewDesc.instances = addSharedPasses(graph, desc);
    for (std::uint32_t view = 0; view < desc.views.size(); ++view)
    {
        graph.beginScope(desc.views[view].name, desc.views[view].extent);
        viewDesc.view = view;
        addScenePasses(graph, viewDesc);
        graph.endScope();
    }
    addOutputPasses(graph, viewDesc);
}
}
