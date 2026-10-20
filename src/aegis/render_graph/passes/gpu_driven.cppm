export module aegis.render_graph:gpu_driven;
import std;
import :draw_batch_registry;
import :error;
import :gpu_types;
import :render_pass;
import :resource;
import :declarations;
import :resource_pool;
import aegis.assets;
import aegis.rhi;
import aegis.math;
import aegis.world;

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
/// @brief Writes "StaticInstances" whenever the scene registry's static version changes, and
///        "DynamicInstances" (DynamicTag entities) and "DrawBatches" every frame. Instance IDs count static
///        instances first, then dynamic ones. The instance buffers grow to the draw batch registry's counts
///        (detail::grownCapacity).
///
///        A static rebuild writes into a new buffer, so frames in flight keep reading the old one, which is
///        freed once they are done. "StaticInstances" has a copy per frame in flight that differ only in the
///        material blocks they address; each frame updates the static materials' block for that frame.
class InstanceUpdatePass : public RenderPass
{
public:
    /// @brief Instances the buffers hold before they first grow.
    static constexpr std::uint32_t initialStaticCapacity{ 65536 };
    static constexpr std::uint32_t initialDynamicCapacity{ 4096 };

    InstanceUpdatePass(ResourceDeclarations& declarations, const DrawBatchRegistry& drawBatches);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto sceneInitialized(ResourcePool& pool, aegis::world::World& scene) -> void override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

    /// @brief The instances actually written: what culling dispatches over.
    [[nodiscard]] auto staticCount() const noexcept -> std::uint32_t { return m_staticCount; }
    [[nodiscard]] auto dynamicCount() const noexcept -> std::uint32_t { return m_dynamicCount; }

private:
    auto rebuildStatic(ResourcePool& pool, aegis::world::World& scene) -> void;

    const DrawBatchRegistry& m_drawBatches;
    ResourceId m_staticInstances;
    ResourceId m_dynamicInstances;
    ResourceId m_drawBatchData;
    std::uint32_t m_staticCapacity{ initialStaticCapacity };
    std::uint32_t m_dynamicCapacity{ initialDynamicCapacity };
    std::uint32_t m_staticCount{ 0 };
    std::uint32_t m_dynamicCount{ 0 };
    std::optional<std::uint64_t> m_builtVersion;                              // The static version the static instances were built for
    std::vector<std::shared_ptr<assets::MaterialInstance>> m_staticMaterials; // Distinct, in the static instances
};

/// @brief Zeroes "IndirectDrawCounts" (one counter per draw batch) before culling counts into it.
class ClearDrawCountsPass : public RenderPass
{
public:
    explicit ClearDrawCountsPass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    ResourceId m_counts;
};

/// @brief Frustum-culls every instance on the GPU, appending the visible ones per draw batch to
///        "VisibleInstances" with their "IndirectDrawCommands", and counting them. Both buffers hold
///        one entry per instance the draw batch registry counts, and grow with it.
class CullingPass : public RenderPass
{
public:
    CullingPass(ResourceDeclarations& declarations, const InstanceUpdatePass& instances);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const InstanceUpdatePass& m_instances;
    ResourceId m_cameraData;
    ResourceId m_staticInstances;
    ResourceId m_dynamicInstances;
    ResourceId m_drawBatchData;
    ResourceId m_visibility;
    ResourceId m_commands;
    ResourceId m_counts;
    std::uint32_t m_visibilityCapacity;
    std::uint32_t m_commandCapacity;
    std::optional<rhi::Pipeline> m_pipeline;
};

/// @brief The GPU-driven G-buffer pass (mesh shaders): one indirect-count draw per draw batch fills
///        the same G-buffer GeometryPass does. Needs rhi::Device::Capabilities::meshShaders.
class GpuDrivenGeometryPass : public RenderPass
{
public:
    GpuDrivenGeometryPass(ResourceDeclarations& declarations, const InstanceUpdatePass& instances);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const InstanceUpdatePass& m_instances;
    std::array<ResourceId, 5> m_gBuffer;
    ResourceId m_depth;
    ResourceId m_cameraData;
    ResourceId m_staticInstances;
    ResourceId m_dynamicInstances;
    ResourceId m_visibility;
    ResourceId m_commands;
    ResourceId m_counts;
    std::optional<rhi::Pipeline> m_pipeline;
};
}
