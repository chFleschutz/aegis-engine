module;
#include <array>
#include <cstddef>
#include <expected>

module aegis.render_graph;
import aegis.rhi;

namespace aegis::render_graph
{
GeometryPass::GeometryPass(ResourcePool& pool) :
    m_gBuffer{
        pool.addTexture("Position", Usage::ColorAttachment, { .format = gBufferFormats[0] }),
        pool.addTexture("Normal", Usage::ColorAttachment, { .format = gBufferFormats[1] }),
        pool.addTexture("Albedo", Usage::ColorAttachment, { .format = gBufferFormats[2] }),
        pool.addTexture("ARM", Usage::ColorAttachment, { .format = gBufferFormats[3] }),
        pool.addTexture("Emissive", Usage::ColorAttachment, { .format = gBufferFormats[4] }),
    },
    m_depth{ pool.addTexture("Depth", Usage::DepthAttachment, { .format = depthFormat }) },
    m_cameraData{ pool.addReference("CameraData", Usage::ShaderRead) }
{
}

auto GeometryPass::info() const -> Info
{
    Info info{ .name = "Geometry", .reads = { m_cameraData } };
    info.writes.assign(m_gBuffer.begin(), m_gBuffer.end());
    info.writes.push_back(m_depth);
    return info;
}

auto GeometryPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    std::array<rhi::Pipeline::ColorAttachment, gBufferFormats.size()> attachments;
    for (std::size_t i = 0; i < gBufferFormats.size(); ++i)
        attachments[i] = { .format = gBufferFormats[i] };

    for (const auto& system : m_renderSystems)
    {
        if (auto created = system->createResources(pool.device(), { attachments, depthFormat }); !created)
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
