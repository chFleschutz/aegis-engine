module;
#include <array>
#include <cstdint>
#include <expected>
#include <optional>

export module aegis.render_graph:gpu_driven;
import :draw_batch_registry;
import :error;
import :render_pass;
import :resource;
import :resource_pool;
import aegis.rhi;
import Aegis.Math;
import Aegis.Scene;

export namespace aegis::render_graph
{
/// @brief One drawable instance as the GPU-driven shaders read it (indirectDraw::Instance).
struct InstanceData
{
    glm::mat3x4 model; // Top three rows of the model matrix
    glm::vec3 normalRow0;
    glm::vec3 normalRow1;
    glm::vec3 normalRow2;
    rhi::DeviceAddress mesh;     // assets::StaticMesh::GpuData
    rhi::DeviceAddress material; // assets::MaterialInstance parameter block
    std::uint32_t drawBatch;
};

/// @brief Where a draw batch's instances live in the visibility and indirect command buffers.
struct DrawBatchData
{
    std::uint32_t offset;
    std::uint32_t count;
};

/// @brief Writes "StaticInstances" once per scene, and "DynamicInstances" (DynamicTag entities) and
///        "DrawBatches" every frame. Instance IDs count static instances first, then dynamic ones.
class InstanceUpdatePass : public RenderPass
{
public:
    static constexpr std::uint32_t maxStaticInstances{ 65536 };
    static constexpr std::uint32_t maxDynamicInstances{ 4096 };

    InstanceUpdatePass(ResourcePool& pool, const DrawBatchRegistry& drawBatches);

    [[nodiscard]] auto info() const -> Info override;
    auto sceneInitialized(ResourcePool& pool, Aegis::Scene::Scene& scene) -> void override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

    /// @brief The instances actually written: what culling dispatches over.
    [[nodiscard]] auto staticCount() const noexcept -> std::uint32_t { return m_staticCount; }
    [[nodiscard]] auto dynamicCount() const noexcept -> std::uint32_t { return m_dynamicCount; }

private:
    const DrawBatchRegistry& m_drawBatches;
    ResourceHandle m_staticInstances;
    ResourceHandle m_dynamicInstances;
    ResourceHandle m_drawBatchData;
    std::uint32_t m_staticCount{ 0 };
    std::uint32_t m_dynamicCount{ 0 };
};

/// @brief Zeroes "IndirectDrawCounts" (one counter per draw batch) before culling counts into it.
class ClearDrawCountsPass : public RenderPass
{
public:
    explicit ClearDrawCountsPass(ResourcePool& pool);

    [[nodiscard]] auto info() const -> Info override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceHandle m_counts;
};

/// @brief Frustum-culls every instance on the GPU, appending the visible ones per draw batch to
///        "VisibleInstances" with their "IndirectDrawCommands", and counting them.
class CullingPass : public RenderPass
{
public:
    CullingPass(ResourcePool& pool, const InstanceUpdatePass& instances);

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const InstanceUpdatePass& m_instances;
    ResourceHandle m_cameraData;
    ResourceHandle m_staticInstances;
    ResourceHandle m_dynamicInstances;
    ResourceHandle m_drawBatchData;
    ResourceHandle m_visibility;
    ResourceHandle m_commands;
    ResourceHandle m_counts;
    std::optional<rhi::Pipeline> m_pipeline;
};

/// @brief The GPU-driven G-buffer pass (mesh shaders): one indirect-count draw per draw batch fills
///        the same G-buffer GeometryPass does. Needs rhi::Device::Capabilities::meshShaders.
class GpuDrivenGeometryPass : public RenderPass
{
public:
    GpuDrivenGeometryPass(ResourcePool& pool, const InstanceUpdatePass& instances);

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const InstanceUpdatePass& m_instances;
    std::array<ResourceHandle, 5> m_gBuffer;
    ResourceHandle m_depth;
    ResourceHandle m_cameraData;
    ResourceHandle m_staticInstances;
    ResourceHandle m_dynamicInstances;
    ResourceHandle m_visibility;
    ResourceHandle m_commands;
    ResourceHandle m_counts;
    std::optional<rhi::Pipeline> m_pipeline;
};
}
