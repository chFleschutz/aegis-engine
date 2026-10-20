module aegis.render_graph;
import std;
import aegis.rhi;

namespace aegis::render_graph
{
GeometryPass::GeometryPass(ResourceDeclarations& declarations) :
    m_gBuffer{ declareGBuffer(declarations) },
    m_depth{ declareDepth(declarations) },
    m_cameraData{ declarations.addReference(names::cameraData, Usage::ShaderRead) }
{
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
    return "Geometry";
}

auto GeometryPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    std::array<rhi::Pipeline::ColorAttachment, gBufferFormats.size()> attachments;
    for (std::size_t i = 0; i < gBufferFormats.size(); ++i)
        attachments[i] = { .format = pool.format(m_gBuffer[i]) };

    for (const auto& system : m_renderSystems)
    {
        if (auto created = system->createResources(pool.device(), { attachments, pool.format(m_depth) }); !created)
            return created;
    }
    return {};
}

auto GeometryPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    std::array<rhi::Attachment, gBufferFormats.size()> colorAttachments;
    for (std::size_t i = 0; i < m_gBuffer.size(); ++i)
        colorAttachments[i] = rhi::Attachment::color(pool.texture(m_gBuffer[i]).view, rhi::ClearColor{});

    const auto& cmd = frameInfo.cmd;
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depth(pool.texture(m_depth).view, rhi::ClearDepthStencil{ 1.0f, 0 }),
    });
    cmd.setViewport(frameInfo.extent);
    cmd.setScissor(frameInfo.extent);

    const RenderContext context{
        .cmd = cmd,
        .scene = frameInfo.scene,
        .frameIndex = frameInfo.frameIndex,
        .camera = pool.bufferAddress(m_cameraData, frameInfo.frameIndex),
    };
    for (const auto& system : m_renderSystems)
        system->render(context);

    cmd.endRendering();
}
}
