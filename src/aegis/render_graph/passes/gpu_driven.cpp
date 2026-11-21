module;
#include <cassert>

module aegis.render_passes;
import std;
import aegis.assets;
import aegis.core;
import aegis.renderer;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
namespace
{
constexpr std::uint32_t initialCapacity = InstanceUpdatePass::initialStaticCapacity + InstanceUpdatePass::initialDynamicCapacity;
constexpr std::size_t indirectCommandSize{ 3 * sizeof(std::uint32_t) }; // VkDrawMeshTasksIndirectCommandEXT

auto gpuKey(Technique technique, const assets::MaterialVariant& variant) -> PipelineKey
{
    // Mirrored instances keep the counter-clockwise front face: the mesh shader flips their winding.
    return { .technique = technique, .variant = variant, .path = GeometryPath::GpuDriven };
}

auto makeInstance(const Instance& instance, std::uint32_t frameIndex) -> InstanceData
{
    assert(instance.mesh && instance.material && "The GPU-driven path draws only instances with a mesh and a material");
    const auto gpuTransform = detail::toGpuTransform(instance.model);
    return {
        .model = gpuTransform.model,
        .normalRow0 = gpuTransform.normalRow0,
        .normalRow1 = gpuTransform.normalRow1,
        .normalRow2 = gpuTransform.normalRow2,
        .mesh = instance.mesh->meshDataAddress(),
        .material = instance.material->parameterAddress(frameIndex),
        .drawBatch = instance.drawBatch,
        .flags = gpuTransform.flags,
    };
}
}

namespace detail
{
auto growBuffer(ResourcePool& pool, ResourceId buffer, std::uint32_t& capacity, std::uint32_t required,
    std::size_t entrySize) -> std::expected<bool, Error>
{
    const auto grown = grownCapacity(capacity, required);
    if (grown == capacity)
        return false;
    if (auto resized = pool.resizeBuffer(buffer, entrySize * grown); !resized)
    {
        auto error = resized.error();
        error.message = std::format("{} entries need {} bytes, the buffer holds {}: {}", required, entrySize * grown,
            entrySize * capacity, error.message);
        return std::unexpected{ std::move(error) };
    }
    capacity = grown;
    return true;
}
}

// Instance update ------------------------------------------------------------------------------------

InstanceUpdatePass::InstanceUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene) :
    m_scene{ scene },
    m_static{
        .buffer = declarations.addBuffer(names::staticInstances, Usage::HostWrite,
            { .size = sizeof(InstanceData) * initialStaticCapacity, .perFrame = true }),
        .capacity = initialStaticCapacity,
    },
    m_dynamic{
        .buffer = declarations.addBuffer(names::dynamicInstances, Usage::HostWrite,
            { .size = sizeof(InstanceData) * initialDynamicCapacity, .perFrame = true }),
        .capacity = initialDynamicCapacity,
    },
    m_drawBatchData{ declarations.addBuffer(names::drawBatches, Usage::HostWrite,
        { .size = sizeof(DrawBatchData) * InstanceStore::maxDrawBatches, .perFrame = true }) }
{
    m_static.pending.fill(DirtyRange::all());
    m_dynamic.pending.fill(DirtyRange::all());
}

auto InstanceUpdatePass::name() const -> std::string_view
{
    return "Instance Update";
}

auto InstanceUpdatePass::prepare(ResourcePool& pool) -> std::expected<void, Error>
{
    const auto& store = m_scene.current().instances;
    if (auto grown = grow(pool, m_static, static_cast<std::uint32_t>(store.staticInstances().size())); !grown)
        return grown;
    return grow(pool, m_dynamic, static_cast<std::uint32_t>(store.dynamicInstances().size()));
}

auto InstanceUpdatePass::grow(ResourcePool& pool, InstanceBuffer& buffer, std::uint32_t count)
    -> std::expected<void, Error>
{
    auto grown = detail::growBuffer(pool, buffer.buffer, buffer.capacity, count, sizeof(InstanceData));
    if (!grown)
        return std::unexpected{ std::move(grown.error()) };
    // A new buffer starts empty in every copy.
    if (*grown)
        buffer.pending.fill(DirtyRange::all());
    return {};
}

// Writes the positions of frame 'frameIndex's copy that changed since it was last written. prepare grew the
// buffer to hold every instance.
auto InstanceUpdatePass::write(ResourcePool& pool, InstanceBuffer& buffer, std::span<const Instance> instances,
    DirtyRange dirty, std::uint32_t frameIndex) -> void
{
    for (auto& pending : buffer.pending)
        pending = pending.merged(dirty);

    buffer.written = static_cast<std::uint32_t>(instances.size());
    assert(buffer.written <= buffer.capacity && "The instance buffer was not grown in prepare");
    const auto range = buffer.pending[frameIndex].clipped(buffer.written);
    buffer.pending[frameIndex] = {};
    if (range.empty())
        return;

    std::vector<InstanceData> data;
    data.reserve(range.size());
    for (const auto& instance : instances.subspan(range.begin, range.size()))
        data.push_back(makeInstance(instance, frameIndex));
    pool.write(buffer.buffer, frameIndex, std::as_bytes(std::span{ data }), sizeof(InstanceData) * range.begin);
}

auto InstanceUpdatePass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& store = m_scene.current().instances;
    // Edits to a material reach the GPU through its block for this frame, which no frame in flight reads
    // any more.
    for (const auto& material : store.materials())
        material->update(frameInfo.frameIndex);

    write(pool, m_static, store.staticInstances(), store.staticDirty(), frameInfo.frameIndex);
    write(pool, m_dynamic, store.dynamicInstances(), store.dynamicDirty(), frameInfo.frameIndex);

    std::vector<DrawBatchData> batches;
    batches.reserve(store.batches().size());
    for (const auto& batch : store.batches())
        batches.push_back({ batch.firstInstance, batch.instanceCount });
    pool.write(m_drawBatchData, frameInfo.frameIndex, std::as_bytes(std::span{ batches }));
}

// Draw count clear -----------------------------------------------------------------------------------

ClearDrawCountsPass::ClearDrawCountsPass(ResourceDeclarations& declarations) :
    m_counts{ declarations.addBuffer(names::indirectDrawCounts, Usage::TransferDst,
        { .size = sizeof(std::uint32_t) * InstanceStore::maxDrawBatches }) }
{
}

auto ClearDrawCountsPass::name() const -> std::string_view
{
    return "Clear Draw Counts";
}

auto ClearDrawCountsPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& counts = pool.buffer(m_counts);
    frameInfo.cmd.fillBuffer(counts, 0, 0, counts.size());
}

// Culling --------------------------------------------------------------------------------------------

CullingPass::CullingPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, const InstanceUpdatePass& instances,
    std::uint32_t view) :
    m_scene{ scene },
    m_instances{ instances },
    m_view{ view },
    m_cameraData{ declarations.addReference(names::cameraData, Usage::ShaderRead) },
    m_staticInstances{ declarations.addReference(names::staticInstances, Usage::ShaderRead) },
    m_dynamicInstances{ declarations.addReference(names::dynamicInstances, Usage::ShaderRead) },
    m_drawBatchData{ declarations.addReference(names::drawBatches, Usage::ShaderRead) },
    m_visibility{ declarations.addBuffer(names::visibleInstances, Usage::StorageWrite, { .size = sizeof(std::uint32_t) * initialCapacity }) },
    m_commands{ declarations.addBuffer(names::indirectDrawCommands, Usage::StorageWrite, { .size = indirectCommandSize * initialCapacity }) },
    m_counts{ declarations.addReference(names::indirectDrawCounts, Usage::StorageWrite) },
    m_visibilityCapacity{ initialCapacity },
    m_commandCapacity{ initialCapacity }
{
}

auto CullingPass::name() const -> std::string_view
{
    return "Culling";
}

auto CullingPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    auto pipeline = createComputePipeline(pool.device(), "Culling", "gpu-driven/culling.slang.spv");
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto CullingPass::prepare(ResourcePool& pool) -> std::expected<void, Error>
{
    // Culling appends each batch's visible instances at its batch offset, so both buffers need an
    // entry per instance in the store.
    const auto registered = m_scene.current().instances.instanceCount();
    if (auto grown = detail::growBuffer(pool, m_visibility, m_visibilityCapacity, registered, sizeof(std::uint32_t)); !grown)
        return std::unexpected{ std::move(grown.error()) };
    if (auto grown = detail::growBuffer(pool, m_commands, m_commandCapacity, registered, indirectCommandSize); !grown)
        return std::unexpected{ std::move(grown.error()) };
    return {};
}

auto CullingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto instanceCount = m_instances.staticCount() + m_instances.dynamicCount();
    if (instanceCount == 0 || !m_scene.current().camera(m_view))
        return;

    const auto frame = frameInfo.frameIndex;
    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(CullingConstants{
        .camera = pool.bufferAddress(m_cameraData, frame),
        .staticInstances = pool.bufferAddress(m_staticInstances, frame),
        .dynamicInstances = pool.bufferAddress(m_dynamicInstances, frame),
        .drawBatches = pool.bufferAddress(m_drawBatchData, frame),
        .visibility = pool.bufferAddress(m_visibility),
        .commands = pool.bufferAddress(m_commands),
        .counts = pool.bufferAddress(m_counts),
        .staticCount = m_instances.staticCount(),
        .dynamicCount = m_instances.dynamicCount(),
    });
    cmd.dispatch(groupCount(instanceCount, cullingGroupSize));
}

// GPU-driven geometry --------------------------------------------------------------------------------

GpuDrivenGeometryPass::GpuDrivenGeometryPass(ResourceDeclarations& declarations, const FrameSceneSource& scene,
    const InstanceUpdatePass& instances, Technique technique, std::uint32_t view) :
    m_scene{ scene },
    m_instances{ instances },
    m_technique{ technique },
    m_view{ view },
    m_gBuffer{ technique == Technique::GBuffer ? std::ranges::to<std::vector>(GeometryPass::declareGBuffer(declarations)) : std::vector<ResourceId>{} },
    m_depth{ GeometryPass::declareDepth(declarations) },
    m_cameraData{ declarations.addReference(names::cameraData, Usage::ShaderRead) },
    m_staticInstances{ declarations.addReference(names::staticInstances, Usage::ShaderRead) },
    m_dynamicInstances{ declarations.addReference(names::dynamicInstances, Usage::ShaderRead) },
    m_visibility{ declarations.addReference(names::visibleInstances, Usage::ShaderRead) },
    m_commands{ declarations.addReference(names::indirectDrawCommands, Usage::IndirectRead) },
    m_counts{ declarations.addReference(names::indirectDrawCounts, Usage::IndirectRead) },
    m_pipelines{ [this](const PipelineKey& key) { return createPipeline(key); } }
{
}

auto GpuDrivenGeometryPass::name() const -> std::string_view
{
    return m_technique == Technique::Depth ? "GPU Driven Depth" : "GPU Driven Geometry";
}

auto GpuDrivenGeometryPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    if (!pool.device().capabilities().meshShaders)
        return makeError(ErrorCode::Unsupported, "The GPU-driven path needs mesh shaders, which this device lacks");

    m_device = &pool.device();
    m_colorAttachments.clear();
    for (const auto texture : m_gBuffer)
        m_colorAttachments.push_back({ .format = pool.format(texture) });
    m_depthAttachment = pool.format(m_depth);
    return {};
}

auto GpuDrivenGeometryPass::prepare(ResourcePool& /*pool*/) -> std::expected<void, Error>
{
    for (const auto& batch : m_scene.current().instances.batches())
    {
        if (!drawsInGBuffer(batch))
            continue;
        if (auto prepared = m_pipelines.prepare(gpuKey(m_technique, batch.variant)); !prepared)
            return prepared;
    }
    return {};
}

auto GpuDrivenGeometryPass::createPipeline(const PipelineKey& key) const -> std::expected<rhi::Pipeline, Error>
{
    const auto state = pipelineState(key);
    auto attachments = m_colorAttachments;
    for (auto& attachment : attachments)
        attachment.blend = state.blend;

    // The task shader is a separate module: sharing one, the mesh stage would also declare the
    // task stage's DrawIndex, which a mesh shader with a task stage must not.
    const auto geometry = techniqueShader(shaderFile, key.variant);
    std::vector stages{
        ShaderEntry{ rhi::ShaderStage::Task, "gpu-driven/task.slang.spv", "taskMain" },
        ShaderEntry{ rhi::ShaderStage::Mesh, geometry, "meshMain" },
    };
    if (const auto fragment = fragmentEntry(key.technique, key.variant))
        stages.push_back(ShaderEntry{ rhi::ShaderStage::Fragment, geometry, *fragment });
    return createGraphicsPipeline(*m_device,
        rhi::Pipeline::GraphicsDesc{
            .name = key.technique == Technique::Depth ? "GpuDrivenDepth" : "GpuDrivenGeometry",
            .colorAttachments = attachments,
            .depthAttachment = m_depthAttachment,
            .cullMode = state.cullMode,
            .frontFace = state.frontFace,
        },
        stages);
}

auto GpuDrivenGeometryPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
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
    // Without a camera (a shadow view without a directional light, or past its cascade count) the targets stay
    // cleared.
    if (!m_scene.current().camera(m_view))
    {
        cmd.endRendering();
        return;
    }

    const auto frame = frameInfo.frameIndex;
    const auto& commands = pool.buffer(m_commands);
    const auto& counts = pool.buffer(m_counts);
    // Culling's prepare grew the command buffer to every batch's maximum draw count.
    assert(m_scene.current().instances.instanceCount() <= pool.bufferSize(m_commands) / indirectCommandSize &&
           "The indirect command buffer was not grown in prepare");
    const rhi::Pipeline* bound{ nullptr };
    for (const auto& batch : m_scene.current().instances.batches())
    {
        // The G-buffer holds opaque surfaces only, as on the CPU path (StaticMeshRenderSystem). A batch that was
        // not prepared has no pipeline and is skipped.
        if (!drawsInGBuffer(batch))
            continue;
        const auto* pipeline = m_pipelines.find(gpuKey(m_technique, batch.variant));
        if (!pipeline)
            continue;
        if (pipeline != bound)
        {
            cmd.bindPipeline(*pipeline);
            bound = pipeline;
        }
        cmd.pushConstants(GpuDrivenGeometryConstants{
            .camera = pool.bufferAddress(m_cameraData, frame),
            .staticInstances = pool.bufferAddress(m_staticInstances, frame),
            .dynamicInstances = pool.bufferAddress(m_dynamicInstances, frame),
            .visibility = pool.bufferAddress(m_visibility),
            .batchFirst = batch.firstInstance,
            .batchSize = batch.instanceCount,
            .staticCount = m_instances.staticCount(),
            .dynamicCount = m_instances.dynamicCount(),
        });
        cmd.drawMeshTasksIndirectCount(commands, indirectCommandSize * batch.firstInstance, counts,
            sizeof(std::uint32_t) * batch.id, batch.instanceCount);
    }
    cmd.endRendering();
}
}
