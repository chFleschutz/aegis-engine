module aegis.render_passes;
import std;
import aegis.core;
import aegis.assets;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
namespace
{
auto cpuKey(const assets::MaterialVariant& variant, bool mirrored) -> PipelineKey
{
    // A mirrored model matrix reverses the winding of every triangle, so its front faces are clockwise.
    return {
        .technique = Technique::GBuffer,
        .variant = variant,
        .path = GeometryPath::CpuDriven,
        .frontFace = mirrored ? rhi::FrontFace::Clockwise : rhi::FrontFace::CounterClockwise,
    };
}
}

StaticMeshRenderSystem::StaticMeshRenderSystem(assets::MaterialType type, std::string technique) :
    m_type{ type },
    m_technique{ std::move(technique) },
    m_pipelines{ [this](const PipelineKey& key) { return createPipeline(key); } }
{
}

auto StaticMeshRenderSystem::createResources(rhi::Device& device, const RenderTargets& targets)
    -> std::expected<void, Error>
{
    m_device = &device;
    m_colorAttachments.assign(targets.colorAttachments.begin(), targets.colorAttachments.end());
    m_depthAttachment = targets.depthAttachment;
    return {};
}

auto StaticMeshRenderSystem::prepare(const FrameScene& frameScene) -> std::expected<void, Error>
{
    for (const auto& batch : frameScene.instances.batches())
    {
        if (!drawsInGBuffer(batch, m_type))
            continue;
        for (const bool mirrored : { false, true })
        {
            if (auto prepared = m_pipelines.prepare(cpuKey(batch.variant, mirrored)); !prepared)
                return prepared;
        }
    }
    return {};
}

auto StaticMeshRenderSystem::render(const RenderContext& context) -> void
{
    const auto& instances = context.frameScene.instances;
    for (const auto& material : instances.materials())
        material->update(context.frameIndex);

    // Per draw batch, its pipelines (normal, mirrored); null for a batch this system skips or that was not prepared.
    std::vector<std::array<const rhi::Pipeline*, 2>> batchPipelines;
    batchPipelines.reserve(instances.batchCount());
    for (const auto& batch : instances.batches())
    {
        if (!drawsInGBuffer(batch, m_type))
            batchPipelines.push_back({ nullptr, nullptr });
        else
            batchPipelines.push_back({ m_pipelines.find(cpuKey(batch.variant, false)), m_pipelines.find(cpuKey(batch.variant, true)) });
    }

    const auto& cmd = context.cmd;
    const rhi::Pipeline* bound{ nullptr };
    const auto draw = [&](const Instance& instance) {
        if (!instance.mesh || !instance.material)
            return;

        const auto gpuTransform = detail::toGpuTransform(instance.model);
        const bool mirrored = core::hasFlag(gpuTransform.flags, InstanceFlags::Mirrored);
        const auto* pipeline = batchPipelines[instance.drawBatch][mirrored ? 1 : 0];
        if (!pipeline)
            return;
        if (pipeline != bound)
        {
            cmd.bindPipeline(*pipeline);
            bound = pipeline;
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

auto StaticMeshRenderSystem::createPipeline(const PipelineKey& key) const -> std::expected<rhi::Pipeline, Error>
{
    const auto state = pipelineState(key);
    auto attachments = m_colorAttachments;
    for (auto& attachment : attachments)
        attachment.blend = state.blend;
    return createGraphicsPipeline(*m_device, techniqueShader(m_technique, *key.variant.materialTemplate),
        rhi::Pipeline::GraphicsDesc{
            .name = key.frontFace == rhi::FrontFace::Clockwise ? "StaticMeshesMirrored" : "StaticMeshes",
            .colorAttachments = attachments,
            .depthAttachment = m_depthAttachment,
            .cullMode = state.cullMode,
            .frontFace = state.frontFace,
        });
}
}
