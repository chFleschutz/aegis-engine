module aegis.render_passes;
import std;
import aegis.rhi;

namespace aegis::render_graph
{
// Spot shadow atlas ----------------------------------------------------------------------------------

SpotShadowAtlasPass::SpotShadowAtlasPass(ResourceDeclarations& declarations) :
    m_atlas{ declarations.addTexture(names::spotShadowAtlas, Usage::DepthAttachment,
        { .format = GeometryPass::depthFormat, .extent = spotShadowAtlasExtent }) }
{
}

auto SpotShadowAtlasPass::name() const -> std::string_view
{
    return "Spot Shadow Atlas";
}

auto SpotShadowAtlasPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // Each spot shadow view clears its own tile too; this clears the tiles no view draws, and gives the atlas its first
    // layout.
    frameInfo.cmd.beginRendering({
        .depthAttachment = rhi::Attachment::depth(pool.texture(m_atlas).view, rhi::ClearDepthStencil{ 1.0f, 0 }),
    });
    frameInfo.cmd.endRendering();
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
        m_gBuffer = std::ranges::to<std::vector>(declareGBuffer(declarations));
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

auto GeometryPass::prepare(ResourcePool& /*pool*/) -> std::expected<void, Error>
{
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

    const RenderContext context{
        .cmd = cmd,
        .frameScene = m_scene.current(),
        .frameIndex = frameInfo.frameIndex,
        .camera = pool.bufferAddress(m_cameraData, frameInfo.frameIndex),
    };
    for (const auto& system : m_renderSystems)
        system->render(context);

    cmd.endRendering();
}
}
