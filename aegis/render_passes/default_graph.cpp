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
        for (std::uint32_t cascade = 0; cascade < maxShadowCascades; ++cascade)
            views.push_back({ .name = shadowViewName(views[i].name, cascade), .extent = shadowMapExtent, .kind = ViewKind::Shadow });
    }

    if (std::ranges::any_of(views, [](const DefaultPassesDesc::View& view) { return view.tile.has_value(); }))
        return;
    for (std::uint32_t index = 0; index < maxShadowedSpotLights; ++index)
        views.push_back({ .name = spotShadowViewName(index), .extent = spotShadowTileExtent, .kind = ViewKind::Shadow, .tile = spotShadowTile(index) });
}

auto viewDeclarationOrder(std::span<const DefaultPassesDesc::View> views) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> order;
    order.reserve(views.size());
    for (const bool tiled : { true, false })
    {
        for (std::uint32_t view = 0; view < views.size(); ++view)
        {
            if (views[view].tile.has_value() == tiled)
                order.push_back(view);
        }
    }
    return order;
}

namespace
{
auto drawsSpotShadows(const DefaultPassesDesc& desc) -> bool
{
    return std::ranges::any_of(desc.views, [](const DefaultPassesDesc::View& view) { return view.tile.has_value(); });
}

// A view's GPU-driven passes: an instance update of its own unless addSharedPasses added the one all views share
// (which creates the unscoped "StaticInstances"), then draw count clear, culling and geometry.
auto addGpuDrivenPasses(GraphBuilder& graph, const DefaultPassesDesc& desc, Technique technique, std::optional<DepthTile> tile) -> void
{
    if (!graph.declarations().find(names::staticInstances))
        graph.add<InstanceUpdatePass>(desc.scene);
    graph.add<ClearDrawCountsPass>();
    graph.add<CullingPass>(desc.scene, desc.pipelines, desc.view);
    graph.add<GpuDrivenGeometryPass>(desc.scene, desc.pipelines, technique, desc.view, std::move(tile));
}
}

auto addSharedPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> SharedPasses
{
    SharedPasses shared{ .lights = &graph.add<LightUpdatePass>(desc.scene) };
    if (drawsSpotShadows(desc))
        graph.add<SpotShadowAtlasPass>();
    if (desc.gpuDriven)
        graph.add<InstanceUpdatePass>(desc.scene);
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
        addGpuDrivenPasses(graph, desc, Technique::GBuffer, std::nullopt);
    else
    {
        graph.add<GeometryPass>(scene, desc.pipelines, Technique::GBuffer, desc.view).addRenderSystem<StaticMeshRenderSystem>();
    }

    std::optional<LightingPass::ShadowMaps> shadow;
    if (view.shadow)
    {
        shadow = LightingPass::ShadowMaps{ .view = *view.shadow };
        for (std::uint32_t cascade = 0; cascade < maxShadowCascades; ++cascade)
            shadow->maps[cascade] = scopedName(desc.views[*view.shadow + cascade].name, names::depth);
        if (drawsSpotShadows(desc))
            shadow->spotAtlas = std::string{ names::spotShadowAtlas };
    }
    graph.add<LightingPass>(scene, desc.pipelines, desc.settings.lighting, desc.view, shadow);
    graph.add<SkyBoxPass>(scene, desc.pipelines, desc.view);
    // Blended draws take the CPU-driven draw on both geometry paths.
    graph.add<ForwardPass>(scene, desc.pipelines, desc.view, shadow).addRenderSystem<StaticMeshRenderSystem>(Technique::Forward);
    graph.add<TransparentPass>(scene, desc.pipelines, desc.view).addRenderSystem<PointLightRenderSystem>();
    graph.add<BloomPass>(scene, desc.pipelines, desc.settings.bloom);
    graph.add<PostProcessingPass>(scene, desc.pipelines);
}

auto addShadowPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    const auto& scene = desc.scene;
    std::optional<DepthTile> tile;
    if (const auto& rect = desc.views[desc.view].tile)
        tile = DepthTile{ .texture = std::string{ names::spotShadowAtlas }, .rect = *rect };
    graph.add<SceneUpdatePass>(scene, desc.view);
    if (desc.gpuDriven)
        addGpuDrivenPasses(graph, desc, Technique::Depth, std::move(tile));
    else
    {
        graph.add<GeometryPass>(scene, desc.pipelines, Technique::Depth, desc.view, std::move(tile)).addRenderSystem<StaticMeshRenderSystem>(Technique::Depth);
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
        graph.add<UiPass>(UiPass::Desc{ .backend = desc.uiBackend, .targetFormat = desc.shownViews->targetFormat, .finals = std::move(finals) });
        return;
    }
    if (desc.ui)
        graph.add<UiPass>(UiPass::Desc{ .backend = desc.uiBackend, .finals = { finals.front() } });
    graph.add<PresentPass>(finals.front());
}

auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    auto viewDesc = desc;
    const auto shared = addSharedPasses(graph, desc);
    viewDesc.lights = shared.lights;
    for (const auto view : viewDeclarationOrder(desc.views))
    {
        graph.beginScope(desc.views[view].name, desc.views[view].extent);
        viewDesc.view = view;
        addScenePasses(graph, viewDesc);
        graph.endScope();
    }
    addOutputPasses(graph, viewDesc);
}
}
