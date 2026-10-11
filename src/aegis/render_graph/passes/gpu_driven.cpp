module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <entt/entt.hpp>

module aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import Aegis.Math;
import Aegis.Scene;

namespace aegis::render_graph
{
namespace
{
constexpr std::uint32_t maxInstances = InstanceUpdatePass::maxStaticInstances + InstanceUpdatePass::maxDynamicInstances;
constexpr std::size_t indirectCommandSize{ 3 * sizeof(std::uint32_t) }; // VkDrawMeshTasksIndirectCommandEXT

auto makeInstance(const Aegis::GlobalTransform& transform, const assets::StaticMesh& mesh,
    const assets::MaterialInstance& material, std::uint32_t frameIndex, std::uint32_t drawBatch) -> InstanceData
{
    const auto model = transform.matrix();
    const auto normalMatrix = glm::inverse(glm::mat3{ model });
    return {
        .model = glm::mat3x4{ glm::rowMajor4(model) },
        .normalRow0 = normalMatrix[0],
        .normalRow1 = normalMatrix[1],
        .normalRow2 = normalMatrix[2],
        .mesh = mesh.meshDataAddress(),
        .material = material.parameterAddress(frameIndex),
        .drawBatch = drawBatch,
    };
}

// Appends the instances of entities with (without) a DynamicTag, up to 'limit'.
template<typename View>
auto collectInstances(View view, const DrawBatchRegistry& drawBatches, std::uint32_t frameIndex, std::uint32_t limit)
    -> std::vector<InstanceData>
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
        { .size = sizeof(InstanceData) * maxStaticInstances }) },
    m_dynamicInstances{ pool.addBuffer("DynamicInstances", Usage::HostWrite,
        { .size = sizeof(InstanceData) * maxDynamicInstances, .perFrame = true }) },
    m_drawBatchData{ pool.addBuffer("DrawBatches", Usage::HostWrite,
        { .size = sizeof(DrawBatchData) * DrawBatchRegistry::maxDrawBatches, .perFrame = true }) }
{
}

auto InstanceUpdatePass::info() const -> Info
{
    return { .name = "Instance Update", .writes = { m_staticInstances, m_dynamicInstances, m_drawBatchData } };
}

auto InstanceUpdatePass::sceneInitialized(ResourcePool& pool, Aegis::Scene::Scene& scene) -> void
{
    // Static instances never change, so their materials' frame 0 block serves every frame.
    auto view = scene.registry().view<Aegis::GlobalTransform, assets::Mesh, assets::Material>(entt::exclude<Aegis::DynamicTag>);
    const auto instances = collectInstances(view, m_drawBatches, 0, maxStaticInstances);
    pool.write(m_staticInstances, 0, std::as_bytes(std::span{ instances }));
    m_staticCount = static_cast<std::uint32_t>(instances.size());
}

auto InstanceUpdatePass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    // DynamicTag is empty, so the view's each() does not yield it.
    auto view = frameInfo.scene.registry().view<Aegis::GlobalTransform, assets::Mesh, assets::Material, Aegis::DynamicTag>();
    const auto instances = collectInstances(view, m_drawBatches, frameInfo.frameIndex, maxDynamicInstances);
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
    m_visibility{ pool.addBuffer("VisibleInstances", Usage::StorageWrite, { .size = sizeof(std::uint32_t) * maxInstances }) },
    m_commands{ pool.addBuffer("IndirectDrawCommands", Usage::StorageWrite, { .size = indirectCommandSize * maxInstances }) },
    m_counts{ pool.addReference("IndirectDrawCounts", Usage::StorageWrite) }
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
    auto pipeline = createComputePipeline(pool.device(), "Culling", "rhi/gpu-driven/culling.slang.spv");
    if (!pipeline)
        return std::unexpected{ pipeline.error() };
    m_pipeline = std::move(*pipeline);
    return {};
}

auto CullingPass::execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void
{
    struct PushConstants
    {
        rhi::DeviceAddress camera;
        rhi::DeviceAddress staticInstances;
        rhi::DeviceAddress dynamicInstances;
        rhi::DeviceAddress drawBatches;
        rhi::DeviceAddress visibility;
        rhi::DeviceAddress commands;
        rhi::DeviceAddress counts;
        std::uint32_t staticCount;
        std::uint32_t dynamicCount;
    };

    const auto instanceCount = m_instances.staticCount() + m_instances.dynamicCount();
    if (instanceCount == 0)
        return;

    const auto frame = frameInfo.frameIndex;
    const auto& cmd = frameInfo.cmd;
    cmd.bindPipeline(*m_pipeline);
    cmd.pushConstants(PushConstants{
        .camera = pool.bufferAddress(m_cameraData, frame),
        .staticInstances = pool.bufferAddress(m_staticInstances),
        .dynamicInstances = pool.bufferAddress(m_dynamicInstances, frame),
        .drawBatches = pool.bufferAddress(m_drawBatchData, frame),
        .visibility = pool.bufferAddress(m_visibility),
        .commands = pool.bufferAddress(m_commands),
        .counts = pool.bufferAddress(m_counts),
        .staticCount = m_instances.staticCount(),
        .dynamicCount = m_instances.dynamicCount(),
    });
    cmd.dispatch(groupCount(instanceCount, 64));
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
        ShaderEntry{ rhi::ShaderStage::Task, "rhi/gpu-driven/task.slang.spv", "taskMain" },
        ShaderEntry{ rhi::ShaderStage::Mesh, "rhi/gpu-driven/geometry.slang.spv", "meshMain" },
        ShaderEntry{ rhi::ShaderStage::Fragment, "rhi/gpu-driven/geometry.slang.spv", "fragmentMain" },
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
    struct PushConstants
    {
        rhi::DeviceAddress camera;
        rhi::DeviceAddress staticInstances;
        rhi::DeviceAddress dynamicInstances;
        rhi::DeviceAddress visibility;
        std::uint32_t batchFirst;
        std::uint32_t batchSize;
        std::uint32_t staticCount;
        std::uint32_t dynamicCount;
    };

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
    for (const auto& batch : frameInfo.drawBatches.batches())
    {
        if (batch.instanceCount == 0)
            continue;
        cmd.pushConstants(PushConstants{
            .camera = pool.bufferAddress(m_cameraData, frame),
            .staticInstances = pool.bufferAddress(m_staticInstances),
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
