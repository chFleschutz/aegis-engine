module;

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

module aegis.render_graph;
import std;
import aegis.assets;
import aegis.renderer;
import aegis.rhi;
import aegis.math;
import aegis.scene;

namespace aegis::render_graph
{
namespace
{
constexpr std::uint32_t initialCapacity = InstanceUpdatePass::initialStaticCapacity + InstanceUpdatePass::initialDynamicCapacity;
constexpr std::size_t indirectCommandSize{ 3 * sizeof(std::uint32_t) }; // VkDrawMeshTasksIndirectCommandEXT

// Grows 'buffer', holding 'capacity' elements of 'elementSize' bytes, until 'required' fit. False when
// the larger buffer could not be created; the old one stays, with its capacity.
auto ensureCapacity(ResourcePool& pool, ResourceHandle buffer, std::uint32_t& capacity, std::uint32_t required,
    std::size_t elementSize) -> bool
{
    const auto grown = detail::grownCapacity(capacity, required);
    if (grown == capacity)
        return true;
    if (auto resized = pool.resizeBuffer(buffer, elementSize * grown); !resized)
    {
        std::println("{}: {} instances need {}, it holds {}", resized.error().message, required,
            elementSize * grown, elementSize * capacity);
        return false;
    }
    capacity = grown;
    return true;
}

auto makeInstance(const aegis::GlobalTransform& transform, const assets::StaticMesh& mesh,
    const assets::MaterialInstance& material, std::uint32_t frameIndex, std::uint32_t drawBatch) -> InstanceData
{
    const auto gpuTransform = detail::toGpuTransform(transform.matrix());
    return {
        .model = gpuTransform.model,
        .normalRow0 = gpuTransform.normalRow0,
        .normalRow1 = gpuTransform.normalRow1,
        .normalRow2 = gpuTransform.normalRow2,
        .mesh = mesh.meshDataAddress(),
        .material = material.parameterAddress(frameIndex),
        .drawBatch = drawBatch,
    };
}

// The instances of the entities in 'view' that can be drawn, up to 'limit', addressing the material
// blocks of frame 'frameIndex'. 'updateMaterials' writes that block first, which is only safe for the
// frame being recorded.
template<typename View>
auto collectInstances(View view, const DrawBatchRegistry& drawBatches, std::uint32_t frameIndex, std::uint32_t limit,
    bool updateMaterials) -> std::vector<InstanceData>
{
    std::vector<InstanceData> instances;
    for (auto&& [entity, transform, mesh, material] : view.each())
    {
        if (instances.size() == limit)
            break;
        if (!mesh.staticMesh || !material.instance)
            continue;
        const auto batch = drawBatches.batchId(*material.instance->materialTemplate());
        if (!batch)
            continue;

        if (updateMaterials)
            material.instance->update(frameIndex);
        instances.push_back(makeInstance(transform, *mesh.staticMesh, *material.instance, frameIndex, *batch));
    }
    return instances;
}
}

// Instance update ------------------------------------------------------------------------------------

InstanceUpdatePass::InstanceUpdatePass(ResourcePool& pool, const DrawBatchRegistry& drawBatches) :
    m_drawBatches{ drawBatches },
    m_staticInstances{ pool.addBuffer("StaticInstances", Usage::HostWrite,
        { .size = sizeof(InstanceData) * initialStaticCapacity, .perFrame = true }) },
    m_dynamicInstances{ pool.addBuffer("DynamicInstances", Usage::HostWrite,
        { .size = sizeof(InstanceData) * initialDynamicCapacity, .perFrame = true }) },
    m_drawBatchData{ pool.addBuffer("DrawBatches", Usage::HostWrite,
        { .size = sizeof(DrawBatchData) * DrawBatchRegistry::maxDrawBatches, .perFrame = true }) }
{
}

auto InstanceUpdatePass::info() const -> Info
{
    return { .name = "Instance Update", .writes = { m_staticInstances, m_dynamicInstances, m_drawBatchData } };
}

auto InstanceUpdatePass::sceneInitialized(ResourcePool&, aegis::scene::Scene&) -> void
{
    // The scene may be a different one, with its own static version: build for it on the next frame.
    m_builtVersion.reset();
}

// Rewrites every frame's copy of the static instances into a new buffer, which frames still in flight
// do not read. Should it not be created, nothing static is drawn until the static set changes again.
auto InstanceUpdatePass::rebuildStatic(ResourcePool& pool, aegis::scene::Scene& scene) -> void
{
    constexpr auto frames = renderer::Renderer::maxFramesInFlight;
    m_staticCount = 0;
    m_staticMaterials.clear();

    const auto capacity = detail::grownCapacity(m_staticCapacity, m_drawBatches.staticInstanceCount());
    if (auto fresh = pool.resizeBuffer(m_staticInstances, sizeof(InstanceData) * capacity); !fresh)
    {
        std::println("{}: {} static instances need {} bytes per frame", fresh.error().message,
            m_drawBatches.staticInstanceCount(), sizeof(InstanceData) * capacity);
        return;
    }
    m_staticCapacity = capacity;

    auto view = scene.registry().view<aegis::GlobalTransform, assets::Mesh, assets::Material>(entt::exclude<aegis::DynamicTag>);
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto instances = collectInstances(view, m_drawBatches, frame, m_staticCapacity, false);
        pool.write(m_staticInstances, frame, std::as_bytes(std::span{ instances }));
        m_staticCount = static_cast<std::uint32_t>(instances.size());
    }

    // Their materials: kept alive for as long as the instances address them, and updated every frame.
    std::unordered_set<const assets::MaterialInstance*> seen;
    for (auto&& [entity, transform, mesh, material] : view.each())
    {
        if (mesh.staticMesh && material.instance && m_drawBatches.batchId(*material.instance->materialTemplate()) &&
            seen.insert(material.instance.get()).second)
            m_staticMaterials.push_back(material.instance);
    }
}

auto InstanceUpdatePass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto version = frameInfo.scene.registry().staticVersion();
    if (m_builtVersion != version)
    {
        m_builtVersion = version;
        rebuildStatic(pool, frameInfo.scene);
    }
    // Edits to a static material reach the GPU through its block for this frame, which no frame in
    // flight reads any more.
    for (const auto& material : m_staticMaterials)
        material->update(frameInfo.frameIndex);

    std::ignore = ensureCapacity(pool, m_dynamicInstances, m_dynamicCapacity, m_drawBatches.dynamicInstanceCount(),
        sizeof(InstanceData));
    // DynamicTag is empty, so the view's each() does not yield it.
    auto view = frameInfo.scene.registry().view<aegis::GlobalTransform, assets::Mesh, assets::Material, aegis::DynamicTag>();
    const auto instances = collectInstances(view, m_drawBatches, frameInfo.frameIndex, m_dynamicCapacity, true);
    pool.write(m_dynamicInstances, frameInfo.frameIndex, std::as_bytes(std::span{ instances }));
    m_dynamicCount = static_cast<std::uint32_t>(instances.size());

    std::vector<DrawBatchData> batches;
    batches.reserve(m_drawBatches.batchCount());
    for (const auto& batch : m_drawBatches.batches())
        batches.push_back({ batch.firstInstance, batch.instanceCount });
    pool.write(m_drawBatchData, frameInfo.frameIndex, std::as_bytes(std::span{ batches }));
}

// Draw count clear -----------------------------------------------------------------------------------

ClearDrawCountsPass::ClearDrawCountsPass(ResourcePool& pool) :
    m_counts{ pool.addBuffer("IndirectDrawCounts", Usage::TransferDst,
        { .size = sizeof(std::uint32_t) * DrawBatchRegistry::maxDrawBatches }) }
{
}

auto ClearDrawCountsPass::info() const -> Info
{
    return { .name = "Clear Draw Counts", .writes = { m_counts } };
}

auto ClearDrawCountsPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    const auto& counts = pool.buffer(m_counts);
    frameInfo.cmd.fillBuffer(counts, 0, 0, counts.size());
}

// Culling --------------------------------------------------------------------------------------------

CullingPass::CullingPass(ResourcePool& pool, const InstanceUpdatePass& instances) :
    m_instances{ instances },
    m_cameraData{ pool.addReference("CameraData", Usage::ShaderRead) },
    m_staticInstances{ pool.addReference("StaticInstances", Usage::ShaderRead) },
    m_dynamicInstances{ pool.addReference("DynamicInstances", Usage::ShaderRead) },
    m_drawBatchData{ pool.addReference("DrawBatches", Usage::ShaderRead) },
    m_visibility{ pool.addBuffer("VisibleInstances", Usage::StorageWrite, { .size = sizeof(std::uint32_t) * initialCapacity }) },
    m_commands{ pool.addBuffer("IndirectDrawCommands", Usage::StorageWrite, { .size = indirectCommandSize * initialCapacity }) },
    m_counts{ pool.addReference("IndirectDrawCounts", Usage::StorageWrite) },
    m_visibilityCapacity{ initialCapacity },
    m_commandCapacity{ initialCapacity }
{
}

auto CullingPass::info() const -> Info
{
    return {
        .name = "Culling",
        .reads = { m_cameraData, m_staticInstances, m_dynamicInstances, m_drawBatchData },
        .writes = { m_visibility, m_commands, m_counts },
    };
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
    // Culling appends each batch's visible instances at its registry offset, so both buffers need an
    // entry per registered instance. Should they fail to grow, nothing is culled: the counts stay zero
    // and nothing is drawn.
    const auto registered = frameInfo.drawBatches.instanceCount();
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

GpuDrivenGeometryPass::GpuDrivenGeometryPass(ResourcePool& pool, const InstanceUpdatePass& instances) :
    m_instances{ instances },
    m_gBuffer{
        pool.addTexture("Position", Usage::ColorAttachment, { .format = GeometryPass::gBufferFormats[0] }),
        pool.addTexture("Normal", Usage::ColorAttachment, { .format = GeometryPass::gBufferFormats[1] }),
        pool.addTexture("Albedo", Usage::ColorAttachment, { .format = GeometryPass::gBufferFormats[2] }),
        pool.addTexture("ARM", Usage::ColorAttachment, { .format = GeometryPass::gBufferFormats[3] }),
        pool.addTexture("Emissive", Usage::ColorAttachment, { .format = GeometryPass::gBufferFormats[4] }),
    },
    m_depth{ pool.addTexture("Depth", Usage::DepthAttachment, { .format = GeometryPass::depthFormat }) },
    m_cameraData{ pool.addReference("CameraData", Usage::ShaderRead) },
    m_staticInstances{ pool.addReference("StaticInstances", Usage::ShaderRead) },
    m_dynamicInstances{ pool.addReference("DynamicInstances", Usage::ShaderRead) },
    m_visibility{ pool.addReference("VisibleInstances", Usage::ShaderRead) },
    m_commands{ pool.addReference("IndirectDrawCommands", Usage::IndirectRead) },
    m_counts{ pool.addReference("IndirectDrawCounts", Usage::IndirectRead) }
{
}

auto GpuDrivenGeometryPass::info() const -> Info
{
    Info info{
        .name = "GPU Driven Geometry",
        .reads = { m_cameraData, m_staticInstances, m_dynamicInstances, m_visibility, m_commands, m_counts },
    };
    info.writes.assign(m_gBuffer.begin(), m_gBuffer.end());
    info.writes.push_back(m_depth);
    return info;
}

auto GpuDrivenGeometryPass::createResources(ResourcePool& pool) -> std::expected<void, Error>
{
    if (!pool.device().capabilities().meshShaders)
        return makeError("The GPU-driven path needs mesh shaders, which this device lacks");

    std::array<rhi::Pipeline::ColorAttachment, GeometryPass::gBufferFormats.size()> attachments;
    for (std::size_t i = 0; i < attachments.size(); ++i)
        attachments[i] = { .format = GeometryPass::gBufferFormats[i] };

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
            .depthAttachment = GeometryPass::depthFormat,
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
    for (const auto& batch : frameInfo.drawBatches.batches())
    {
        if (batch.instanceCount == 0 || std::size_t{ batch.firstInstance } + batch.instanceCount > commandCapacity)
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
