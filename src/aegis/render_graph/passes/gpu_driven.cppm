module;
#include <array>
#include <bit>
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

export namespace aegis::render_graph::detail
{
/// @brief The capacity a growable buffer moves to so that 'required' entries fit: 'current' when they
///        already do, else the next power of two (or 'required' itself beyond 2^31). Never shrinks.
[[nodiscard]] constexpr auto grownCapacity(std::uint32_t current, std::uint32_t required) noexcept -> std::uint32_t
{
    if (required <= current)
        return current;
    constexpr std::uint32_t largestPowerOfTwo{ 1u << 31 };
    return required > largestPowerOfTwo ? required : std::bit_ceil(required);
}
}

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
///        The instance buffers grow to the draw batch registry's counts (detail::grownCapacity).
class InstanceUpdatePass : public RenderPass
{
public:
    /// @brief Instances the buffers hold before they first grow.
    static constexpr std::uint32_t initialStaticCapacity{ 65536 };
    static constexpr std::uint32_t initialDynamicCapacity{ 4096 };

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
    std::uint32_t m_staticCapacity{ initialStaticCapacity };
    std::uint32_t m_dynamicCapacity{ initialDynamicCapacity };
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
///        "VisibleInstances" with their "IndirectDrawCommands", and counting them. Both buffers hold
///        one entry per instance the draw batch registry counts, and grow with it.
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
    std::uint32_t m_visibilityCapacity;
    std::uint32_t m_commandCapacity;
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
