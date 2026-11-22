export module aegis.render_passes:gpu_driven;
import std;
import :frame_scene;
import :geometry_pass;
import :instance_store;
import :gpu_types;
import :pipeline_cache;
import :pipeline_library;
import aegis.render_graph;
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

/// @brief Grows 'buffer', which holds 'capacity' entries of 'entrySize' bytes, until 'required' entries fit
///        (grownCapacity); its contents are not kept. For a pass's prepare: execute then records into a buffer
///        that holds the frame.
/// @return Whether the buffer grew, or the pool's error with the sizes; the old buffer and 'capacity' stay then.
[[nodiscard]] auto growBuffer(ResourcePool& pool, ResourceId buffer, std::uint32_t& capacity, std::uint32_t required,
    std::size_t entrySize) -> std::expected<bool, Error>;
}

export namespace aegis::render_graph
{
/// @brief Writes the frame's InstanceStore into "StaticInstances" and "DynamicInstances", and its draw batches
///        into "DrawBatches". Instance IDs count static instances first, then dynamic ones.
///
///        Both instance buffers have a copy per frame in flight, which differ only in the material blocks they
///        address. A frame rewrites only the positions of its copy that changed since that copy was last
///        written (the store's dirty ranges, collected for each copy), so a frame still in flight never sees
///        a write. The buffers grow to the store's counts in prepare (detail::growBuffer), and a grown buffer is
///        written whole. Each frame also updates the store's materials' blocks for that frame.
class InstanceUpdatePass : public RenderPass
{
public:
    /// @brief Instances the buffers hold before they first grow.
    static constexpr std::uint32_t initialStaticCapacity{ 65536 };
    static constexpr std::uint32_t initialDynamicCapacity{ 4096 };

    InstanceUpdatePass(ResourceDeclarations& declarations, const FrameSceneSource& scene);

    [[nodiscard]] auto name() const -> std::string_view override;
    /// @brief Grows the instance buffers to the frame's store.
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
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

    [[nodiscard]] static auto grow(ResourcePool& pool, InstanceBuffer& buffer, std::uint32_t count)
        -> std::expected<void, Error>;
    static auto write(ResourcePool& pool, InstanceBuffer& buffer, std::span<const Instance> instances,
        DirtyRange dirty, std::uint32_t frameIndex) -> void;

    const FrameSceneSource& m_scene;
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
///        one entry per instance of the frame's InstanceStore, and grow with it. Without a camera for its view it
///        culls nothing, and the geometry pass draws nothing.
class CullingPass : public RenderPass
{
public:
    /// @param view The index of the view in FrameScene::views it culls for (whose "CameraData" it reads).
    CullingPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        const InstanceUpdatePass& instances, std::uint32_t view = 0);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    /// @brief Grows the visibility and command buffers to the frame's store.
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const FrameSceneSource& m_scene;
    PipelineLibrary& m_pipelines;
    const InstanceUpdatePass& m_instances;
    std::uint32_t m_view;
    ResourceId m_cameraData;
    ResourceId m_staticInstances;
    ResourceId m_dynamicInstances;
    ResourceId m_drawBatchData;
    ResourceId m_visibility;
    ResourceId m_commands;
    ResourceId m_counts;
    std::uint32_t m_visibilityCapacity;
    std::uint32_t m_commandCapacity;
    const rhi::Pipeline* m_pipeline{ nullptr };
};

/// @brief The GPU-driven geometry pass (mesh shaders): one indirect-count draw per opaque draw batch, with that
///        batch's pipeline for its technique, fills the same G-buffer GeometryPass does, or only "Depth" with
///        Technique::Depth (a shadow view's), from the instances StaticMeshRenderSystem draws on the CPU path. Without
///        a camera for its view it clears its targets and draws nothing. Needs rhi::Device::Capabilities::meshShaders.
class GpuDrivenGeometryPass : public RenderPass
{
public:
    /// @brief The shader file both techniques' mesh and fragment stages are in (techniqueShader).
    static constexpr std::string_view shaderFile{ "gpu_driven/geometry" };

    /// @param view The index of the view in FrameScene::views it draws (whose "CameraData" it reads).
    /// @param tile Where a Technique::Depth pass draws instead of its own "Depth" (GeometryPass's); none: its own.
    GpuDrivenGeometryPass(ResourceDeclarations& declarations, const FrameSceneSource& scene, PipelineLibrary& pipelines,
        const InstanceUpdatePass& instances, Technique technique = Technique::GBuffer, std::uint32_t view = 0,
        std::optional<DepthTile> tile = std::nullopt);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    /// @brief Takes the pipelines of the frame's draw batches it draws from its PipelineLibrary.
    [[nodiscard]] auto prepare(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

    [[nodiscard]] auto technique() const noexcept -> Technique { return m_technique; }
    [[nodiscard]] auto pipelines() const noexcept -> const PipelineCache<>& { return m_pipelines; }

private:
    [[nodiscard]] auto findPipeline(const PipelineKey& key) const -> std::expected<const rhi::Pipeline*, Error>;

    const FrameSceneSource& m_scene;
    PipelineLibrary& m_pipelineLibrary;
    const InstanceUpdatePass& m_instances;
    Technique m_technique;
    std::uint32_t m_view;
    std::vector<ResourceId> m_gBuffer; // Empty with Technique::Depth
    std::optional<DepthTile> m_tile;
    ResourceId m_depth;
    ResourceId m_cameraData;
    ResourceId m_staticInstances;
    ResourceId m_dynamicInstances;
    ResourceId m_visibility;
    ResourceId m_commands;
    ResourceId m_counts;
    rhi::Device* m_device{ nullptr };
    std::vector<rhi::Pipeline::ColorAttachment> m_colorAttachments;
    rhi::Format m_depthAttachment{ rhi::Format::Unknown };
    PipelineCache<> m_pipelines;
};
}
