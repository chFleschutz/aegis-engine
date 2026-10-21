module aegis.render_graph;
import std;
import aegis.base;
import aegis.assets;
import aegis.rhi;
import aegis.math;

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
    const auto& instances = context.frameScene.instances;
    for (const auto& material : instances.materials())
        material->update(context.frameIndex);

    const auto& cmd = context.cmd;
    cmd.bindPipeline(*m_pipeline);
    bool boundMirrored{ false };

    const auto& batches = instances.batches();
    const auto draw = [&](const Instance& instance) {
        if (!instance.mesh || !instance.material || batches[instance.drawBatch].materialTemplate->type() != m_type)
            return;

        const auto gpuTransform = detail::toGpuTransform(instance.transform);
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
            .mesh = instance.mesh->meshDataAddress(),
            .material = instance.material->parameterAddress(context.frameIndex),
        });

        cmd.bindIndexBuffer(m_device->get(instance.mesh->indexBuffer()));
        cmd.drawIndexed(instance.mesh->indexCount());
    };
    for (const auto& instance : instances.staticInstances())
        draw(instance);
    for (const auto& instance : instances.dynamicInstances())
        draw(instance);
}
}
