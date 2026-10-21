export module aegis.render_graph:gpu_driven;
import std;
import :error;
import :gpu_types;
import :instance_store;
import :render_pass;
import :resource;
import :declarations;
import :resource_pool;
import aegis.assets;
import aegis.rhi;
import aegis.math;
import aegis.renderer;

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
/// @brief Writes the frame's InstanceStore into "StaticInstances" and "DynamicInstances", and its draw batches
///        into "DrawBatches". Instance IDs count static instances first, then dynamic ones.
///
///        Both instance buffers have a copy per frame in flight, which differ only in the material blocks they
///        address. A frame rewrites only the positions of its copy that changed since that copy was last
///        written (the store's dirty ranges, collected for each copy), so a frame still in flight never sees
///        a write. The buffers grow to the store's counts (detail::grownCapacity), and a grown buffer is
///        written whole. Each frame also updates the store's materials' blocks for that frame.
class InstanceUpdatePass : public RenderPass
{
public:
    /// @brief Instances the buffers hold before they first grow.
    static constexpr std::uint32_t initialStaticCapacity{ 65536 };
    static constexpr std::uint32_t initialDynamicCapacity{ 4096 };

    explicit InstanceUpdatePass(ResourceDeclarations& declarations);

    [[nodiscard]] auto name() const -> std::string_view override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

    /// @brief The instances actually written: what culling dispatches over.
    [[nodiscard]] auto staticCount() const noexcept -> std::uint32_t { return m_static.written; }
    [[nodiscard]] auto dynamicCount() const noexcept -> std::uint32_t { return m_dynamic.written; }

private:
    struct InstanceBuffer
    {
        ResourceId buffer;
        std::uint32_t capacity;
        // Per frame copy: the positions changed since it was last written; all of them at first.
        std::array<DirtyRange, renderer::Renderer::maxFramesInFlight> pending;
        std::uint32_t written{ 0 }; // Instances in the copy last written
    };

    static auto write(ResourcePool& pool, InstanceBuffer& buffer, std::span<const Instance> instances,
        DirtyRange dirty, std::uint32_t frameIndex) -> void;

    InstanceBuffer m_static;
    InstanceBuffer m_dynamic;
    ResourceId m_drawBatchData;
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
///        one entry per instance of the frame's InstanceStore, and grow with it.
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

/// @brief The GPU-driven G-buffer pass (mesh shaders): one indirect-count draw per opaque draw batch fills
///        the same G-buffer GeometryPass does, from the instances StaticMeshRenderSystem draws on the CPU path. Needs rhi::Device::Capabilities::meshShaders.
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
