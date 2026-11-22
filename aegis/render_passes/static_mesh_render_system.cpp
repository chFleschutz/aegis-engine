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
auto cpuKey(Technique technique, const assets::MaterialVariant& variant, bool mirrored) -> PipelineKey
{
    // A mirrored model matrix reverses the winding of every triangle, so its front faces are clockwise.
    return {
        .technique = technique,
        .variant = variant,
        .path = GeometryPath::CpuDriven,
        .frontFace = mirrored ? rhi::FrontFace::Clockwise : rhi::FrontFace::CounterClockwise,
    };
}
}

StaticMeshRenderSystem::StaticMeshRenderSystem(Technique technique, assets::MaterialType type) :
    m_technique{ technique },
    m_type{ type },
    m_pipelines{ [this](const PipelineKey& key) { return findPipeline(key); } }
{
}

auto StaticMeshRenderSystem::createResources(PipelineLibrary& pipelines, rhi::Device& device, const RenderTargets& targets)
    -> std::expected<void, Error>
{
    m_pipelineLibrary = &pipelines;
    m_device = &device;
    m_colorAttachments.assign(targets.colorAttachments.begin(), targets.colorAttachments.end());
    m_depthAttachment = targets.depthAttachment;
    return {};
}

auto StaticMeshRenderSystem::prepare(const FrameScene& frameScene) -> std::expected<void, Error>
{
    for (const auto& batch : frameScene.instances.batches())
    {
        if (!drawsBatch(m_technique, batch, m_type))
            continue;
        for (const bool mirrored : { false, true })
        {
            if (auto prepared = m_pipelines.prepare(cpuKey(m_technique, batch.variant, mirrored)); !prepared)
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
    const auto find = [&](const assets::MaterialVariant& variant, bool mirrored) -> const rhi::Pipeline* {
        const auto* found = m_pipelines.find(cpuKey(m_technique, variant, mirrored));
        return found ? *found : nullptr;
    };
    std::vector<std::array<const rhi::Pipeline*, 2>> batchPipelines;
    batchPipelines.reserve(instances.batches().size());
    for (const auto& batch : instances.batches())
    {
        if (!drawsBatch(m_technique, batch, m_type))
            batchPipelines.push_back({ nullptr, nullptr });
        else
            batchPipelines.push_back({ find(batch.variant, false), find(batch.variant, true) });
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
        const StaticMeshConstants constants{
            .model = gpuTransform.model,
            .normalRow0 = gpuTransform.normalRow0,
            .normalRow1 = gpuTransform.normalRow1,
            .normalRow2 = gpuTransform.normalRow2,
            .camera = context.camera,
            .mesh = instance.mesh->meshDataAddress(),
            .material = instance.material->parameterAddress(context.frameIndex),
        };
        if (m_technique == Technique::Forward)
        {
            cmd.pushConstants(ForwardConstants{
                .model = constants.model,
                .normalRow0 = constants.normalRow0,
                .normalRow1 = constants.normalRow1,
                .normalRow2 = constants.normalRow2,
                .camera = constants.camera,
                .mesh = constants.mesh,
                .material = constants.material,
                .lighting = context.lighting,
            });
        }
        else
        {
            cmd.pushConstants(constants);
        }

        cmd.bindIndexBuffer(m_device->get(instance.mesh->indexBuffer()));
        cmd.drawIndexed(instance.mesh->indexCount());
    };
    const auto staticInstances = instances.staticInstances();
    const auto dynamicInstances = instances.dynamicInstances();
    if (m_technique != Technique::Forward)
    {
        for (const auto& instance : staticInstances)
            draw(instance);
        for (const auto& instance : dynamicInstances)
            draw(instance);
        return;
    }

    // Blended draws compose only back to front: sort the ones this system draws by their bounding sphere's view depth.
    const auto instanceAt = [&](std::uint32_t index) -> const Instance& {
        return index < staticInstances.size() ? staticInstances[index] : dynamicInstances[index - staticInstances.size()];
    };
    m_order.clear();
    const auto count = static_cast<std::uint32_t>(staticInstances.size() + dynamicInstances.size());
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto& instance = instanceAt(i);
        if (!instance.mesh || !instance.material || !batchPipelines[instance.drawBatch][0])
            continue;
        const auto center = detail::worldCenter(instance.model, instance.mesh->bounds().center);
        m_order.push_back({ .depth = detail::viewDepth(context.view, center), .index = i });
    }
    detail::sortBackToFront(m_order);
    for (const auto& entry : m_order)
        draw(instanceAt(entry.index));
}

auto StaticMeshRenderSystem::findPipeline(const PipelineKey& key) const -> std::expected<const rhi::Pipeline*, Error>
{
    const auto state = pipelineState(key);
    auto attachments = m_colorAttachments;
    for (auto& attachment : attachments)
        attachment.blend = state.blend;

    const auto shader = techniqueShader(key.technique == Technique::Forward ? forwardShaderFile : shaderFile, key.variant);
    std::vector stages{ ShaderEntry{ rhi::ShaderStage::Vertex, shader, "vertexMain" } };
    if (const auto fragment = fragmentEntry(key.technique, key.variant))
        stages.push_back(ShaderEntry{ rhi::ShaderStage::Fragment, shader, *fragment });

    const bool mirrored = key.frontFace == rhi::FrontFace::Clockwise;
    std::string_view name{ mirrored ? "StaticMeshesMirrored" : "StaticMeshes" };
    if (key.technique == Technique::Depth)
        name = mirrored ? "StaticMeshDepthMirrored" : "StaticMeshDepth";
    else if (key.technique == Technique::Forward)
        name = mirrored ? "StaticMeshForwardMirrored" : "StaticMeshForward";
    return m_pipelineLibrary->graphics(*m_device,
        rhi::Pipeline::GraphicsDesc{
            .name = name,
            .colorAttachments = attachments,
            .depthAttachment = m_depthAttachment,
            .depth = { .test = true, .write = state.depthWrite },
            .cullMode = state.cullMode,
            .frontFace = state.frontFace,
        },
        stages);
}
}
