module aegis.render_passes;

namespace aegis::render_graph
{
auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    graph.add<SceneUpdatePass>();
    if (desc.gpuDriven)
    {
        const auto& instances = graph.add<InstanceUpdatePass>();
        graph.add<ClearDrawCountsPass>();
        graph.add<CullingPass>(instances);
        graph.add<GpuDrivenGeometryPass>(instances);
    }
    else
    {
        graph.add<GeometryPass>().addRenderSystem<StaticMeshRenderSystem>();
    }

    graph.add<LightingPass>();
    graph.add<SkyBoxPass>();
    graph.add<TransparentPass>().addRenderSystem<PointLightRenderSystem>();
    graph.add<BloomPass>();
    graph.add<PostProcessingPass>();
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
}
