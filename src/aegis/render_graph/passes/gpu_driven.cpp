module;
#include <cassert>

module aegis.render_graph;
import std;
import aegis.assets;
import aegis.base;
import aegis.renderer;
import aegis.rhi;
import aegis.math;

namespace aegis::render_graph
{
namespace
{
constexpr std::uint32_t initialCapacity = InstanceUpdatePass::initialStaticCapacity + InstanceUpdatePass::initialDynamicCapacity;
constexpr std::size_t indirectCommandSize{ 3 * sizeof(std::uint32_t) }; // VkDrawMeshTasksIndirectCommandEXT

// Grows 'buffer', holding 'capacity' elements of 'elementSize' bytes, until 'required' fit. False when
// the larger buffer could not be created; the old one stays, with its capacity.
auto ensureCapacity(ResourcePool& pool, ResourceId buffer, std::uint32_t& capacity, std::uint32_t required,
    std::size_t elementSize) -> bool
{
    const auto grown = detail::grownCapacity(capacity, required);
    if (grown == capacity)
        return true;
    if (auto resized = pool.resizeBuffer(buffer, elementSize * grown); !resized)
    {
        log::warn("{}: {} instances need {}, it holds {}", describe(resized.error()), required,
            elementSize * grown, elementSize * capacity);
        return false;
    }
    capacity = grown;
    return true;
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

// Instance update ------------------------------------------------------------------------------------

InstanceUpdatePass::InstanceUpdatePass(ResourceDeclarations& declarations) :
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

// Writes the positions of frame 'frameIndex's copy that changed since it was last written. Should the
// buffer fail to grow, only the instances that fit are written (and drawn).
auto InstanceUpdatePass::write(ResourcePool& pool, InstanceBuffer& buffer, std::span<const Instance> instances,
    DirtyRange dirty, std::uint32_t frameIndex) -> void
{
    for (auto& pending : buffer.pending)
        pending = pending.merged(dirty);

    const auto count = static_cast<std::uint32_t>(instances.size());
    const auto capacity = buffer.capacity;
    // A new buffer starts empty in every copy.
    if (ensureCapacity(pool, buffer.buffer, buffer.capacity, count, sizeof(InstanceData)) && buffer.capacity != capacity)
        buffer.pending.fill(DirtyRange::all());

    buffer.written = std::min(count, buffer.capacity);
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
    const auto& store = frameInfo.frameScene.instances;
    // Edits to a material reach the GPU through its block for this frame, which no frame in flight reads
    // any more.
    for (const auto& material : store.materials())
        material->update(frameInfo.frameIndex);

    write(pool, m_static, store.staticInstances(), store.staticDirty(), frameInfo.frameIndex);
    write(pool, m_dynamic, store.dynamicInstances(), store.dynamicDirty(), frameInfo.frameIndex);

    std::vector<DrawBatchData> batches;
    batches.reserve(store.batchCount());
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

CullingPass::CullingPass(ResourceDeclarations& declarations, const InstanceUpdatePass& instances) :
    m_instances{ instances },
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

auto CullingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // Culling appends each batch's visible instances at its batch offset, so both buffers need an
    // entry per instance in the store. Should they fail to grow, nothing is culled: the counts stay zero
    // and nothing is drawn.
    const auto registered = frameInfo.frameScene.instances.instanceCount();
    if (!ensureCapacity(pool, m_visibility, m_visibilityCapacity, registered, sizeof(std::uint32_t)) ||
        !ensureCapacity(pool, m_commands, m_commandCapacity, registered, indirectCommandSize))
        return;

    const auto instanceCount = m_instances.staticCount() + m_instances.dynamicCount();
    if (instanceCount == 0)
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

GpuDrivenGeometryPass::GpuDrivenGeometryPass(ResourceDeclarations& declarations, const InstanceUpdatePass& instances) :
    m_instances{ instances },
    m_gBuffer{ GeometryPass::declareGBuffer(declarations) },
    m_depth{ GeometryPass::declareDepth(declarations) },
    m_cameraData{ declarations.addReference(names::cameraData, Usage::ShaderRead) },
    m_staticInstances{ declarations.addReference(names::staticInstances, Usage::ShaderRead) },
    m_dynamicInstances{ declarations.addReference(names::dynamicInstances, Usage::ShaderRead) },
    m_visibility{ declarations.addReference(names::visibleInstances, Usage::ShaderRead) },
    m_commands{ declarations.addReference(names::indirectDrawCommands, Usage::IndirectRead) },
    m_counts{ declarations.addReference(names::indirectDrawCounts, Usage::IndirectRead) }
{
}

auto GpuDrivenGeometryPass::name() const -> std::string_view
{
    return "GPU Driven Geometry";
}

auto GpuDrivenGeometryPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    if (!pool.device().capabilities().meshShaders)
        return makeError(ErrorCode::Unsupported, "The GPU-driven path needs mesh shaders, which this device lacks");

    std::array<rhi::Pipeline::ColorAttachment, GeometryPass::gBufferFormats.size()> attachments;
    for (std::size_t i = 0; i < attachments.size(); ++i)
        attachments[i] = { .format = pool.format(m_gBuffer[i]) };

    // The task shader is a separate module: sharing one, the mesh stage would also declare the
    // task stage's DrawIndex, which a mesh shader with a task stage must not.
    constexpr std::array stages{
        ShaderEntry{ rhi::ShaderStage::Task, "gpu-driven/task.slang.spv", "taskMain" },
        ShaderEntry{ rhi::ShaderStage::Mesh, "gpu-driven/geometry.slang.spv", "meshMain" },
        ShaderEntry{ rhi::ShaderStage::Fragment, "gpu-driven/geometry.slang.spv", "fragmentMain" },
    };
    auto pipeline = createGraphicsPipeline(pool.device(),
        rhi::Pipeline::GraphicsDesc{
            .name = "GpuDrivenGeometry",
            .colorAttachments = attachments,
            .depthAttachment = pool.format(m_depth),
        },
        stages);
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto GpuDrivenGeometryPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    std::array<rhi::Attachment, GeometryPass::gBufferFormats.size()> colorAttachments;
    for (std::size_t i = 0; i < m_gBuffer.size(); ++i)
        colorAttachments[i] = rhi::Attachment::color(pool.texture(m_gBuffer[i]).view, rhi::ClearColor{});

    const auto& cmd = frameInfo.cmd;
    cmd.beginRendering({
        .colorAttachments = colorAttachments,
        .depthAttachment = rhi::Attachment::depth(pool.texture(m_depth).view, rhi::ClearDepthStencil{ 1.0f, 0 }),
    });
    cmd.setViewport(frameInfo.extent);
    cmd.setScissor(frameInfo.extent);
    cmd.bindPipeline(*m_pipeline);

    const auto frame = frameInfo.frameIndex;
    const auto& commands = pool.buffer(m_commands);
    const auto& counts = pool.buffer(m_counts);
    // A batch past the command buffer's end (it failed to grow; culling then wrote nothing) is
    // skipped: its maximum draw count would read beyond the buffer.
    const auto commandCapacity = pool.bufferSize(m_commands) / indirectCommandSize;
    for (const auto& batch : frameInfo.frameScene.instances.batches())
    {
        // The G-buffer holds opaque surfaces only, as on the CPU path (StaticMeshRenderSystem).
        if (batch.instanceCount == 0 || batch.materialTemplate->type() != assets::MaterialType::Opaque ||
            std::size_t{ batch.firstInstance } + batch.instanceCount > commandCapacity)
            continue;
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
