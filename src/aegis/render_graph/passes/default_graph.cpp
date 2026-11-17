module aegis.render_passes;

namespace aegis::render_graph
{
auto addScenePasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    const auto& scene = desc.scene;
    graph.add<SceneUpdatePass>(scene);
    if (desc.gpuDriven)
    {
        const auto& instances = graph.add<InstanceUpdatePass>(scene);
        graph.add<ClearDrawCountsPass>();
        graph.add<CullingPass>(scene, instances);
        graph.add<GpuDrivenGeometryPass>(scene, instances);
    }
    else
    {
        graph.add<GeometryPass>(scene).addRenderSystem<StaticMeshRenderSystem>();
    }

    graph.add<LightingPass>(scene);
    graph.add<SkyBoxPass>(scene);
    graph.add<TransparentPass>(scene).addRenderSystem<PointLightRenderSystem>();
    graph.add<BloomPass>(scene);
    graph.add<PostProcessingPass>(scene);
}

auto addOutputPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    if (desc.view)
    {
        // The frame's target shows only the UI; "Final" stays the view's, for the UI to show.
        graph.add<UiPass>(UiPass::Desc{ .context = desc.context, .targetFormat = desc.view->targetFormat });
        return;
    }
    if (desc.ui)
        graph.add<UiPass>(UiPass::Desc{ .context = desc.context });
    graph.add<PresentPass>();
}

auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    addScenePasses(graph, desc);
    addOutputPasses(graph, desc);
}
}
