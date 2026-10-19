export module aegis.assets:static_mesh;
import std;
import :error;
import :gpu_types;
import :mesh_data;
import aegis.rhi;
import aegis.core.asset;

export namespace aegis::assets
{
/// @brief A mesh in GPU buffers, reached by shaders through buffer device addresses.
/// @note Holds the Device and frees its buffers on destruction (deferred until the GPU is done).
class StaticMesh : public aegis::core::Asset
{
public:
    /// @brief What meshDataAddress() points at; the layout shaders read (scalar).
    using GpuData = MeshGpuData;

    /// @brief Creates the buffers and queues their uploads; usable once the next
    ///        Device::flushUploads has completed.
    [[nodiscard]] static auto create(rhi::Device& device, const MeshData& data, std::string_view name = {})
        -> std::expected<std::shared_ptr<StaticMesh>, Error>;

    StaticMesh(const StaticMesh&) = delete;
    StaticMesh(StaticMesh&&) = delete;
    ~StaticMesh() override;

    auto operator=(const StaticMesh&) -> StaticMesh& = delete;
    auto operator=(StaticMesh&&) -> StaticMesh& = delete;

    [[nodiscard]] auto vertexCount() const noexcept -> std::uint32_t { return m_gpuData.vertexCount; }
    [[nodiscard]] auto indexCount() const noexcept -> std::uint32_t { return m_gpuData.indexCount; }
    [[nodiscard]] auto meshletCount() const noexcept -> std::uint32_t { return m_gpuData.meshletCount; }
    [[nodiscard]] auto bounds() const noexcept -> const BoundingSphere& { return m_gpuData.bounds; }
    [[nodiscard]] auto gpuData() const noexcept -> const GpuData& { return m_gpuData; }
    /// @brief The 32-bit index buffer, for drawing with CommandBuffer::bindIndexBuffer / drawIndexed.
    [[nodiscard]] auto indexBuffer() const noexcept -> rhi::BufferHandle { return m_buffers[Indices]; }

    /// @brief Address of the GpuData block, the one value a draw needs to reach the whole mesh.
    [[nodiscard]] auto meshDataAddress() const -> rhi::DeviceAddress;

private:
    enum BufferIndex
    {
        Vertices,
        Indices,
        Meshlets,
        MeshletVertices,
        MeshletPrimitives,
        MeshDataBlock,
        BufferCount,
    };

    StaticMesh(rhi::Device& device, std::array<rhi::BufferHandle, BufferCount> buffers, const GpuData& gpuData);

    rhi::Device& m_device;
    std::array<rhi::BufferHandle, BufferCount> m_buffers;
    GpuData m_gpuData;
};
}
