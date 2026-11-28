module aegis.render_passes;
import std;
import aegis.math;
import aegis.rhi;

namespace aegis::render_graph
{
// Spot shadow atlas ----------------------------------------------------------------------------------

auto addSpotShadowAtlasPass(GraphBuilder& graph) -> void
{
    graph.addPass("Spot Shadow Atlas", [](ResourceDeclarations& declarations) {
        const auto atlas = declarations.addTexture(names::spotShadowAtlas, Usage::DepthAttachment,
            { .format = GeometryPass::depthFormat, .extent = spotShadowAtlasExtent });
        return [atlas](ResourcePool& pool, const FrameInfo& frameInfo) {
            // Each spot shadow view clears its own tile too; this clears the tiles no view draws, and gives the atlas
            // its first layout.
            frameInfo.cmd.beginRendering({
                .depthAttachment = rhi::Attachment::depth(pool.texture(atlas).view, rhi::ClearDepthStencil{ 1.0f, 0 }),
            });
            frameInfo.cmd.endRendering();
        };
    });
}

// Geometry -------------------------------------------------------------------------------------------

GeometryPass::GeometryPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
    Technique technique, std::uint32_t view, std::optional<DepthTile> tile) :
    m_scene{ scene },
    m_pipelines{ pipelines },
    m_technique{ technique },
    m_view{ view },
    m_tile{ std::move(tile) }
{
    if (technique == Technique::GBuffer)
    {
        m_gBuffer = std::ranges::to<std::vector>(declareGBuffer(declarations));
        m_previousModels = declarations.addBuffer(names::previousModels, Usage::HostWrite,
            { .size = sizeof(math::Float3x4) * initialPreviousModelCapacity, .perFrame = true });
    }
    m_depth = declareDepth(declarations, m_tile);
    m_cameraData = declarations.addReference(names::cameraData, Usage::ShaderRead);
}

auto GeometryPass::declareGBuffer(ResourceDeclarations& declarations) -> std::array<ResourceId, gBufferFormats.size()>
{
    return {
        declarations.addTexture(names::position, Usage::ColorAttachment, { .format = gBufferFormats[0] }),
        declarations.addTexture(names::normal, Usage::ColorAttachment, { .format = gBufferFormats[1] }),
        declarations.addTexture(names::albedo, Usage::ColorAttachment, { .format = gBufferFormats[2] }),
        declarations.addTexture(names::arm, Usage::ColorAttachment, { .format = gBufferFormats[3] }),
        declarations.addTexture(names::emissive, Usage::ColorAttachment, { .format = gBufferFormats[4] }),
        declarations.addTexture(names::motionVectors, Usage::ColorAttachment, { .format = gBufferFormats[5] }),
    };
}

auto GeometryPass::declareDepth(ResourceDeclarations& declarations, const std::optional<DepthTile>& tile) -> ResourceId
{
    if (tile)
        return declarations.addReference(tile->texture, Usage::DepthAttachment);
    return declarations.addTexture(names::depth, Usage::DepthAttachment, { .format = depthFormat });
}

auto GeometryPass::depthArea(const std::optional<DepthTile>& tile, rhi::Extent2D extent) -> rhi::Rect2D
{
    return tile ? tile->rect : rhi::Rect2D{ .extent = extent };
}

auto GeometryPass::name() const -> std::string_view
{
    return m_technique == Technique::Depth ? "Depth" : "Geometry";
}

auto GeometryPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    std::vector<rhi::Pipeline::ColorAttachment> attachments;
    for (const auto texture : m_gBuffer)
        attachments.push_back({ .format = pool.format(texture) });

    for (const auto& system : m_renderSystems)
    {
        if (auto created = system->createResources(m_pipelines, pool.device(), { attachments, pool.format(m_depth) }); !created)
            return created;
    }
    return {};
}

auto GeometryPass::prepare(ResourcePool& pool) -> std::expected<void, Error>
{
    if (m_previousModels)
    {
        const auto count = m_scene.current().instances.instanceCount();
        if (auto grown = detail::growBuffer(pool, *m_previousModels, m_previousModelCapacity, count, sizeof(math::Float3x4)); !grown)
            return std::unexpected{ std::move(grown.error()) };
    }
    for (const auto& system : m_renderSystems)
    {
        if (auto prepared = system->prepare(m_scene.current()); !prepared)
            return prepared;
    }
    return {};
}

auto GeometryPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    std::vector<rhi::Attachment> colorAttachments;
    for (const auto texture : m_gBuffer)
        colorAttachments.push_back(rhi::Attachment::color(pool.texture(texture).view, rhi::ClearColor{}));

    const auto& cmd = frameInfo.cmd;
    const auto area = depthArea(m_tile, pool.extent(m_depth));
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depth(pool.texture(m_depth).view, rhi::ClearDepthStencil{ 1.0f, 0 }),
        .area = area,
    });
    cmd.setViewport(area);
    cmd.setScissor(area);
    // Without a camera (a shadow view without a directional light, or past its cascade count) the targets stay
    // cleared.
    if (!m_scene.current().camera(m_view))
    {
        cmd.endRendering();
        return;
    }

    rhi::DeviceAddress previousModels{ 0 };
    if (m_previousModels)
    {
        // This frame's copy, which no frame in flight reads; prepare grew it to the store.
        const auto& instances = m_scene.current().instances;
        m_previousModelRows.clear();
        for (const auto set : { instances.staticInstances(), instances.dynamicInstances() })
        {
            for (const auto& instance : set)
                m_previousModelRows.push_back(instance.previousModel);
        }
        if (!m_previousModelRows.empty())
            pool.write(*m_previousModels, std::as_bytes(std::span{ m_previousModelRows }));
        previousModels = pool.bufferAddress(*m_previousModels);
    }

    const RenderContext context{
        .cmd = cmd,
        .frameScene = m_scene.current(),
        .frameIndex = frameInfo.frameIndex,
        .camera = pool.bufferAddress(m_cameraData),
        .previousModels = previousModels,
    };
    for (const auto& system : m_renderSystems)
        system->render(context);

    cmd.endRendering();
}
}
