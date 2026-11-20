module aegis.render_passes;
import std;
import aegis.rhi;

namespace aegis::render_graph
{
GeometryPass::GeometryPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, Technique technique,
    std::uint32_t view) :
    m_scene{ scene },
    m_technique{ technique },
    m_view{ view }
{
    if (technique == Technique::GBuffer)
        m_gBuffer = std::ranges::to<std::vector>(declareGBuffer(declarations));
    m_depth = declareDepth(declarations);
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

auto GeometryPass::declareDepth(ResourceDeclarations& declarations) -> ResourceId
{
    return declarations.addTexture(names::depth, Usage::DepthAttachment, { .format = depthFormat });
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
        if (auto created = system->createResources(pool.device(), { attachments, pool.format(m_depth) }); !created)
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
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depth(pool.texture(m_depth).view, rhi::ClearDepthStencil{ 1.0f, 0 }),
    });
    cmd.setViewport(pool.extent(m_depth));
    cmd.setScissor(pool.extent(m_depth));
    // Without a camera (a shadow view without a directional light) the targets stay cleared.
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
