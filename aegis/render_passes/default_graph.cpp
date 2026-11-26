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

auto frameSceneViews(std::span<const DefaultPassesDesc::View> views) -> std::vector<FrameScene::View>
{
    std::vector<FrameScene::View> sceneViews;
    sceneViews.reserve(views.size());
    for (const auto& view : views)
        sceneViews.push_back({ .name = view.name, .extent = view.extent, .kind = view.kind, .shadow = view.shadow, .tile = view.tile });
    return sceneViews;
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
// (which creates the unscoped "StaticInstances"), then culling, draw compaction and geometry.
auto addGpuDrivenPasses(GraphBuilder& graph, const DefaultPassesDesc& desc, std::uint32_t view, Technique technique, std::optional<DepthTile> tile) -> void
{
    if (!graph.declarations().find(names::staticInstances))
        graph.add<InstanceUpdatePass>(desc.scene);
    graph.add<CullingPass>(desc.scene, desc.pipelines, view);
    graph.add<DrawCompactionPass>(desc.scene, desc.pipelines, view);
    graph.add<GpuDrivenGeometryPass>(desc.scene, desc.pipelines, technique, view, std::move(tile));
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

auto addScenePasses(GraphBuilder& graph, const DefaultPassesDesc& desc, const ViewContext& view) -> void
{
    const auto& described = desc.views[view.index];
    if (described.kind == ViewKind::Shadow)
    {
        addShadowPasses(graph, desc, view);
        return;
    }

    const auto& scene = desc.scene;
    graph.add<SceneUpdatePass>(scene, view.index);
    if (!desc.lights)
        graph.add<LightUpdatePass>(scene);
    if (desc.gpuDriven)
        addGpuDrivenPasses(graph, desc, view.index, Technique::GBuffer, std::nullopt);
    else
    {
        graph.add<GeometryPass>(scene, desc.pipelines, Technique::GBuffer, view.index).addRenderSystem<StaticMeshRenderSystem>();
    }

    std::optional<LightingPass::ShadowMaps> shadow;
    if (described.shadow)
    {
        shadow = LightingPass::ShadowMaps{ .view = *described.shadow };
        for (std::uint32_t cascade = 0; cascade < maxShadowCascades; ++cascade)
            shadow->maps[cascade] = scopedName(desc.views[*described.shadow + cascade].name, names::depth);
        if (drawsSpotShadows(desc))
            shadow->spotAtlas = std::string{ names::spotShadowAtlas };
    }
    graph.add<LightingPass>(scene, desc.pipelines, desc.settings.lighting, view.index, shadow);
    graph.add<SkyBoxPass>(scene, desc.pipelines, view.index);
    // Blended draws take the CPU-driven draw on both geometry paths.
    graph.add<ForwardPass>(scene, desc.pipelines, view.index, shadow).addRenderSystem<StaticMeshRenderSystem>(Technique::Forward);
    graph.add<TransparentPass>(scene, desc.pipelines, view.index).addRenderSystem<PointLightRenderSystem>();
    graph.add<BloomPass>(scene, desc.pipelines, desc.settings.bloom);
    graph.add<PostProcessingPass>(scene, desc.pipelines, PostProcessingPass::Desc{ .targetEncodes = desc.targetEncodes() });
}

auto addShadowPasses(GraphBuilder& graph, const DefaultPassesDesc& desc, const ViewContext& view) -> void
{
    const auto& scene = desc.scene;
    std::optional<DepthTile> tile;
    if (const auto& rect = desc.views[view.index].tile)
        tile = DepthTile{ .texture = std::string{ names::spotShadowAtlas }, .rect = *rect };
    graph.add<SceneUpdatePass>(scene, view.index);
    if (desc.gpuDriven)
        addGpuDrivenPasses(graph, desc, view.index, Technique::Depth, std::move(tile));
    else
    {
        graph.add<GeometryPass>(scene, desc.pipelines, Technique::Depth, view.index, std::move(tile)).addRenderSystem<StaticMeshRenderSystem>(Technique::Depth);
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
        graph.add<UiPass>(UiPass::Desc{ .backend = desc.uiBackend, .targetFormat = desc.targetFormat, .finals = std::move(finals) });
        return;
    }
    if (desc.ui)
        graph.add<UiPass>(UiPass::Desc{ .backend = desc.uiBackend, .finals = { finals.front() } });
    graph.add<PresentPass>(desc.targetFormat, finals.front());
}

auto addDefaultPasses(GraphBuilder& graph, const DefaultPassesDesc& desc) -> void
{
    auto viewDesc = desc;
    viewDesc.lights = addSharedPasses(graph, desc).lights;
    for (const auto index : viewDeclarationOrder(desc.views))
    {
        const auto view = ViewContext::of(desc.views, index);
        graph.beginScope(view.name, view.extent);
        addScenePasses(graph, viewDesc, view);
        graph.endScope();
    }
    addOutputPasses(graph, viewDesc);
}
}
