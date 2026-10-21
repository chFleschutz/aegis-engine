module aegis.render_graph;
import std;
import aegis.base;
import aegis.assets;
import aegis.world.render_components;
import aegis.rhi;
import aegis.math;
import aegis.world;

namespace aegis::render_graph
{
StaticMeshRenderSystem::StaticMeshRenderSystem(assets::MaterialType type, std::string shader) :
    m_type{ type },
    m_shader{ std::move(shader) }
{
}

auto StaticMeshRenderSystem::createResources(rhi::Device& device, const RenderTargets& targets)
    -> std::expected<void, Error>
{
    m_device = &device;
    auto pipeline = createGraphicsPipeline(device, m_shader,
        rhi::Pipeline::GraphicsDesc{
            .name = "StaticMeshes",
            .colorAttachments = targets.colorAttachments,
            .depthAttachment = targets.depthAttachment,
        });
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    // A mirrored model matrix reverses the winding of every triangle, so its front faces are clockwise.
    auto mirrored = createGraphicsPipeline(device, m_shader,
        rhi::Pipeline::GraphicsDesc{
            .name = "StaticMeshesMirrored",
            .colorAttachments = targets.colorAttachments,
            .depthAttachment = targets.depthAttachment,
            .frontFace = rhi::FrontFace::Clockwise,
        });
    if (!mirrored)
        return std::unexpected{ mirrored.error() };
    m_pipeline = std::move(*pipeline);
    m_mirroredPipeline = std::move(*mirrored);
    return {};
}

auto StaticMeshRenderSystem::render(const RenderContext& context) -> void
{
    const auto& cmd = context.cmd;
    cmd.bindPipeline(*m_pipeline);
    bool boundMirrored{ false };

    const assets::Material* lastMaterial{ nullptr };
    auto view = context.scene.registry().view<aegis::GlobalTransform, aegis::MeshRenderer>();
    for (auto&& [entity, transform, renderer] : view.each())
    {
        if (!renderer.mesh || !renderer.material || renderer.material->materialTemplate()->type() != m_type)
            continue;

        if (lastMaterial != renderer.material.get())
        {
            renderer.material->update(context.frameIndex);
            lastMaterial = renderer.material.get();
        }

        const auto gpuTransform = detail::toGpuTransform(transform.matrix());
        if (const bool mirrored = base::hasFlag(gpuTransform.flags, InstanceFlags::Mirrored); mirrored != boundMirrored)
        {
            cmd.bindPipeline(mirrored ? *m_mirroredPipeline : *m_pipeline);
            boundMirrored = mirrored;
        }
        cmd.pushConstants(StaticMeshConstants{
            .model = gpuTransform.model,
            .normalRow0 = gpuTransform.normalRow0,
            .normalRow1 = gpuTransform.normalRow1,
            .normalRow2 = gpuTransform.normalRow2,
            .camera = context.camera,
            .mesh = renderer.mesh->meshDataAddress(),
            .material = renderer.material->parameterAddress(context.frameIndex),
        });

        cmd.bindIndexBuffer(m_device->get(renderer.mesh->indexBuffer()));
        cmd.drawIndexed(renderer.mesh->indexCount());
    }
}
}
