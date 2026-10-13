module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

export module aegis.render_graph:resource_pool;
import :error;
import :resource;
import aegis.renderer;
import aegis.rhi;

export namespace aegis::render_graph
{
class FrameGraph;

/// @brief One resource pool entry as tools show it (the editor's renderer panel).
struct ResourceDescription
{
    enum class Kind
    {
        Texture,
        Buffer,
        Reference,
    };

    std::string_view name;
    Kind kind;
    Usage usage;
    std::optional<rhi::Format> format; // Textures only
};

/// @brief The frame graph's resource declarations and, once compiled, the resources behind them.
///        Passes declare in their constructor and look resources up while executing.
class ResourcePool
{
    friend class FrameGraph;

public:
    ResourcePool(rhi::Device& device, renderer::Renderer& renderer);
    ResourcePool(const ResourcePool&) = delete;
    ResourcePool(ResourcePool&&) = delete;
    ~ResourcePool();

    auto operator=(const ResourcePool&) -> ResourcePool& = delete;
    auto operator=(ResourcePool&&) -> ResourcePool& = delete;

    [[nodiscard]] auto device() const noexcept -> rhi::Device& { return m_device; }
    [[nodiscard]] auto renderer() const noexcept -> renderer::Renderer& { return m_renderer; }

    /// @brief Declares a texture this pass creates. Names are unique among created resources.
    auto addTexture(std::string name, Usage usage, const TextureDesc& desc) -> ResourceHandle;
    /// @brief Declares a buffer this pass creates. Names are unique among created resources.
    auto addBuffer(std::string name, Usage usage, const BufferDesc& desc) -> ResourceHandle;
    /// @brief Declares an access to a resource another pass creates, found by name at compile time.
    auto addReference(std::string name, Usage usage) -> ResourceHandle;

    /// @brief Every declaration, in the order passes made them.
    [[nodiscard]] auto describe() const -> std::vector<ResourceDescription>;
    [[nodiscard]] auto name(ResourceHandle handle) const -> std::string_view { return m_entries[handle.index].name; }

    // Valid after FrameGraph::compile

    [[nodiscard]] auto texture(ResourceHandle handle) const -> const renderer::Texture&;
    [[nodiscard]] auto extent(ResourceHandle handle) const -> rhi::Extent2D;
    [[nodiscard]] auto buffer(ResourceHandle handle) const -> const rhi::Buffer&;

    /// @brief The address of the buffer, or of frame 'frameIndex's copy of a per-frame buffer.
    [[nodiscard]] auto bufferAddress(ResourceHandle handle, std::uint32_t frameIndex = 0) const -> rhi::DeviceAddress;
    /// @brief The byte offset of frame 'frameIndex's copy (0 for a buffer that is not per-frame).
    [[nodiscard]] auto bufferOffset(ResourceHandle handle, std::uint32_t frameIndex = 0) const -> std::size_t;

    /// @brief Writes into frame 'frameIndex's copy of a buffer declared with Usage::HostWrite.
    auto write(ResourceHandle handle, std::uint32_t frameIndex, std::span<const std::byte> data,
        std::size_t offset = 0) const -> void;

private:
    struct Reference
    {
        std::optional<std::uint32_t> target;
    };

    struct Entry
    {
        std::string name;
        Usage usage;
        std::variant<TextureDesc, BufferDesc, Reference> info;
    };

    struct CreatedTexture
    {
        renderer::Texture texture;
        bool ownedByPool; // Fixed size; resolution-dependent ones belong to the renderer
    };

    struct CreatedBuffer
    {
        rhi::BufferHandle buffer;
        std::size_t stride; // Distance between per-frame copies; 0 when not per-frame
    };

    [[nodiscard]] auto resolveReferences() -> std::expected<void, Error>;
    [[nodiscard]] auto createResources() -> std::expected<void, Error>;
    [[nodiscard]] auto actual(ResourceHandle handle) const -> std::uint32_t;

    rhi::Device& m_device;
    renderer::Renderer& m_renderer;
    std::vector<Entry> m_entries;
    std::vector<std::variant<std::monostate, CreatedTexture, CreatedBuffer>> m_created; // Per entry
};
}
