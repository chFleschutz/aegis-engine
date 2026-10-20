export module aegis.render_graph:resource_pool;
import std;
import :declarations;
import :error;
import :resource;
import aegis.renderer;
import aegis.rhi;

export namespace aegis::render_graph
{
class FrameGraph;

/// @brief The resources behind the frame graph's declarations, once compiled. It owns every one of them and
///        frees them when destroyed. Passes look their resources up here while executing.
class ResourcePool
{
    friend class FrameGraph;

public:
    /// @param output The extent textures without a fixed one are sized by (usually the renderer's).
    ResourcePool(rhi::Device& device, const ResourceDeclarations& declarations, rhi::Extent2D output);
    ResourcePool(const ResourcePool&) = delete;
    ResourcePool(ResourcePool&&) = delete;
    ~ResourcePool();

    auto operator=(const ResourcePool&) -> ResourcePool& = delete;
    auto operator=(ResourcePool&&) -> ResourcePool& = delete;

    [[nodiscard]] auto device() const noexcept -> rhi::Device& { return m_device; }
    /// @brief The extent textures that follow the output are sized by.
    [[nodiscard]] auto output() const noexcept -> rhi::Extent2D { return m_output; }

    /// @brief Every declaration, in the order passes made them.
    [[nodiscard]] auto describe() const -> std::vector<ResourceDescription> { return m_declarations.describe(); }
    [[nodiscard]] auto name(ResourceHandle handle) const -> std::string_view { return m_declarations.name(handle); }

    // Valid after FrameGraph::compile

    [[nodiscard]] auto texture(ResourceHandle handle) const -> const renderer::Texture&;
    [[nodiscard]] auto extent(ResourceHandle handle) const -> rhi::Extent2D;
    /// @brief The format the texture was declared with, so a pass need not repeat it.
    [[nodiscard]] auto format(ResourceHandle handle) const -> rhi::Format;
    /// @brief The storage image slot of the texture's view, for a texture some pass declares with a storage
    ///        usage (Usage::StorageRead or StorageWrite).
    [[nodiscard]] auto storageDescriptor(ResourceHandle handle) const -> rhi::StorageImageDescriptor;
    [[nodiscard]] auto buffer(ResourceHandle handle) const -> const rhi::Buffer&;

    /// @brief The address of the buffer, or of frame 'frameIndex's copy of a per-frame buffer.
    [[nodiscard]] auto bufferAddress(ResourceHandle handle, std::uint32_t frameIndex = 0) const -> rhi::DeviceAddress;
    /// @brief The byte offset of frame 'frameIndex's copy (0 for a buffer that is not per-frame).
    [[nodiscard]] auto bufferOffset(ResourceHandle handle, std::uint32_t frameIndex = 0) const -> std::size_t;

    /// @brief Writes into frame 'frameIndex's copy of a buffer declared with Usage::HostWrite.
    auto write(ResourceHandle handle, std::uint32_t frameIndex, std::span<const std::byte> data,
        std::size_t offset = 0) const -> void;

    /// @brief The size of one copy of the buffer (of one frame's copy, for a per-frame buffer).
    [[nodiscard]] auto bufferSize(ResourceHandle handle) const -> std::size_t;
    /// @brief Replaces the buffer with a new one of 'size' bytes per copy, with the same usage and
    ///        memory. The handle stays valid; the old buffer is freed once the GPU is done with it, and
    ///        its contents are not copied. Passes fetch buffers and addresses per frame, so the next
    ///        lookup sees the new one; barriers on buffers are global, so they need no update.
    [[nodiscard]] auto resizeBuffer(ResourceHandle handle, std::size_t size) -> std::expected<void, Error>;

private:
    struct CreatedTexture
    {
        renderer::Texture texture;
        rhi::ImageUsage usage; // The merged usage it was created with, kept to recreate it
    };

    struct CreatedBuffer
    {
        rhi::BufferHandle buffer;
        std::size_t stride; // Distance between per-frame copies; 0 when not per-frame
        rhi::BufferUsage usage;
        rhi::MemoryUsage memory;
        std::size_t size; // Of one copy
    };

    [[nodiscard]] auto createBuffer(const std::string& name, const BufferDesc& desc, rhi::BufferUsage usage,
        rhi::MemoryUsage memory) const -> std::expected<CreatedBuffer, Error>;
    [[nodiscard]] auto createResources(const GraphPlan& plan) -> std::expected<void, Error>;
    /// @brief Recreates the textures that follow the output at the new extent; their handles stay valid.
    /// @warning The GPU must be idle (the renderer waits when it resizes).
    [[nodiscard]] auto resize(rhi::Extent2D output) -> std::expected<void, Error>;
    [[nodiscard]] auto actual(ResourceHandle handle) const -> std::uint32_t;
    /// @brief The description of the texture entry 'entry' at the current output extent.
    [[nodiscard]] auto textureDesc(std::uint32_t entry, rhi::ImageUsage usage) const -> renderer::Texture::Desc;

    rhi::Device& m_device;
    const ResourceDeclarations& m_declarations;
    rhi::Extent2D m_output;
    std::vector<std::uint32_t> m_targets;                                               // Per entry: the entry creating its resource
    std::vector<std::variant<std::monostate, CreatedTexture, CreatedBuffer>> m_created; // Per entry
};
}
