module aegis.render_passes;

namespace aegis::render_graph
{
auto addShadowViews(std::vector<DefaultPassesDesc::View>& views) -> void
{
    const auto mainViews = static_cast<std::uint32_t>(views.size());
    for (std::uint32_t i = 0; i < mainViews; ++i)
    {
        if (views[i].kind != ViewKind::Main || views[i].shadow)
            continue;
        views[i].shadow = static_cast<std::uint32_t>(views.size());
        views.push_back({ .name = shadowViewName(views[i].name), .extent = shadowMapExtent, .kind = ViewKind::Shadow });
    }
}

auto addSharedPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> SharedPasses
{
    SharedPasses shared{ .lights = &graph.add<LightUpdatePass>(desc.scene) };
    if (desc.gpuDriven)
        shared.instances = &graph.add<InstanceUpdatePass>(desc.scene);
    return shared;
}

auto addScenePasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    const auto& view = desc.views[desc.view];
    if (view.kind == ViewKind::Shadow)
    {
        addShadowPasses(graph, desc);
        return;
    }

    const auto& scene = desc.scene;
    graph.add<SceneUpdatePass>(scene, desc.view);
    if (!desc.lights)
        graph.add<LightUpdatePass>(scene);
    if (desc.gpuDriven)
    {
        const auto& instances = desc.instances ? *desc.instances : graph.add<InstanceUpdatePass>(scene);
        graph.add<ClearDrawCountsPass>();
        graph.add<CullingPass>(scene, instances);
        graph.add<GpuDrivenGeometryPass>(scene, instances, Technique::GBuffer, desc.view);
    }
    else
    {
        graph.add<GeometryPass>(scene, Technique::GBuffer, desc.view).addRenderSystem<StaticMeshRenderSystem>();
    }

    std::optional<LightingPass::ShadowMap> shadow;
    if (view.shadow)
        shadow = { .view = *view.shadow, .map = scopedName(desc.views[*view.shadow].name, names::depth) };
    graph.add<LightingPass>(scene, desc.view, std::move(shadow));
    graph.add<SkyBoxPass>(scene, desc.view);
    graph.add<TransparentPass>(scene).addRenderSystem<PointLightRenderSystem>();
    graph.add<BloomPass>(scene);
    graph.add<PostProcessingPass>(scene);
}

auto addShadowPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    const auto& scene = desc.scene;
    graph.add<SceneUpdatePass>(scene, desc.view);
    if (desc.gpuDriven)
    {
        const auto& instances = desc.instances ? *desc.instances : graph.add<InstanceUpdatePass>(scene);
        graph.add<ClearDrawCountsPass>();
        graph.add<CullingPass>(scene, instances);
        graph.add<GpuDrivenGeometryPass>(scene, instances, Technique::Depth, desc.view);
    }
    else
    {
        graph.add<GeometryPass>(scene, Technique::Depth, desc.view).addRenderSystem<StaticMeshRenderSystem>(Technique::Depth);
    }
}

auto addOutputPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    std::vector<std::string> finals;
    for (const auto& view : desc.views)
    {
        if (view.kind == ViewKind::Main)
            finals.push_back(scopedName(view.name, names::finalImage));
    }

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
    const auto shared = addSharedPasses(graph, desc);
    viewDesc.instances = shared.instances;
    viewDesc.lights = shared.lights;
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
