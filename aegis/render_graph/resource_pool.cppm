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

/// @brief The resources behind the frame graph's declarations, once compiled. It owns every one the graph
///        creates and frees them when destroyed; imported ones stay their owner's. Passes look their resources
///        up here while executing, each frame: the image behind a history pair's id changes every frame.
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
    /// @brief The extent unscoped textures that follow the output are sized by.
    [[nodiscard]] auto output() const noexcept -> rhi::Extent2D { return m_output; }
    /// @brief The extent the textures of the scope 'scope' (an index into ResourceDeclarations::scopes) that have none
    ///        of their own are sized by: the scope's, or the output's. Valid once FrameGraph::create returned.
    [[nodiscard]] auto scopeExtent(std::uint32_t scope) const -> rhi::Extent2D;

    /// @brief Every declaration, in the order passes made them.
    [[nodiscard]] auto describe() const -> std::vector<ResourceDescription> { return m_declarations.describe(); }
    [[nodiscard]] auto name(ResourceId handle) const -> std::string_view { return m_declarations.name(handle); }

    // Valid once FrameGraph::create returned

    /// @brief The texture behind the declaration. Of a history pair: this frame's image, or the previous
    ///        frame's for an id from ResourceDeclarations::addHistoryReference.
    [[nodiscard]] auto texture(ResourceId handle) const -> const renderer::Texture&;
    /// @brief Whether a persistent texture or a history pair holds nothing of the previous frame yet: in the
    ///        first frame, after a resize recreated it and after a graph rebuild. A pass then starts over with
    ///        what it accumulates. False for every other resource.
    [[nodiscard]] auto isFresh(ResourceId handle) const -> bool;
    [[nodiscard]] auto extent(ResourceId handle) const -> rhi::Extent2D;
    /// @brief The format the texture was declared with, so a pass need not repeat it.
    [[nodiscard]] auto format(ResourceId handle) const -> rhi::Format;
    /// @brief The storage image slot of the texture's view, for a texture some pass declares with a storage
    ///        usage (Usage::StorageRead or StorageWrite).
    [[nodiscard]] auto storageDescriptor(ResourceId handle) const -> rhi::StorageImageDescriptor;
    /// @brief A view of mip 'mip' alone of a texture the graph creates with more than one mip, with the bindless
    ///        descriptors its merged usage allows. Of a history pair: of the image texture() returns. The handle stays
    ///        valid across a resize, which recreates the view with the texture; a resize to a tiny extent can leave
    ///        the image fewer mips than declared, so ask the image for its count.
    [[nodiscard]] auto mipView(ResourceId handle, std::uint32_t mip) const -> rhi::ImageViewHandle;
    [[nodiscard]] auto buffer(ResourceId handle) const -> const rhi::Buffer&;

    // Valid only while FrameGraph::execute records a frame: they pick the copy of the frame it records

    /// @brief The address of the buffer, or of the recorded frame's copy of a per-frame buffer.
    [[nodiscard]] auto bufferAddress(ResourceId handle) const -> rhi::DeviceAddress;
    /// @brief The byte offset of the recorded frame's copy (0 for a buffer that is not per-frame).
    [[nodiscard]] auto bufferOffset(ResourceId handle) const -> std::size_t;
    /// @brief Writes into the recorded frame's copy of a buffer declared with Usage::HostWrite.
    auto write(ResourceId handle, std::span<const std::byte> data, std::size_t offset = 0) const -> void;

    /// @brief The size of one copy of the buffer (of one frame's copy, for a per-frame buffer).
    [[nodiscard]] auto bufferSize(ResourceId handle) const -> std::size_t;
    /// @brief Replaces the buffer with a new one of 'size' bytes per copy, with the same usage and
    ///        memory. The handle stays valid; the old buffer is freed once the GPU is done with it, and
    ///        its contents are not copied. Passes fetch buffers and addresses per frame, so the next
    ///        lookup sees the new one; barriers on buffers are global, so they need no update.
    [[nodiscard]] auto resizeBuffer(ResourceId handle, std::size_t size) -> std::expected<void, Error>;

private:
    struct CreatedTexture
    {
        std::array<renderer::Texture, 2> textures;                 // A history pair has two, every other texture the first
        std::array<std::vector<rhi::ImageViewHandle>, 2> mipViews; // Per image: a view per mip, for more than one mip
        Lifetime lifetime;
        rhi::ImageUsage usage; // The merged usage it was created with, kept to recreate it
        bool fresh{ true };    // No frame ran since it was created or recreated
    };

    struct ImportedTexture
    {
        std::optional<renderer::Texture> texture; // None until bound
    };

    struct ImportedBuffer
    {
        std::optional<rhi::BufferHandle> buffer; // None until bound
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
    /// @brief Recreates the textures that follow the output at the new extent (not those of a scope with an extent
    ///        of its own); their handles stay valid.
    /// @warning The GPU must be idle (the renderer waits when it resizes).
    [[nodiscard]] auto resize(rhi::Extent2D output) -> std::expected<void, Error>;
    /// @brief Gives the scope 'scope' the extent 'extent' and recreates its textures that follow it, like resize.
    [[nodiscard]] auto resize(std::uint32_t scope, rhi::Extent2D extent) -> std::expected<void, Error>;
    /// @brief Recreates the textures without an extent of their own whose scope (none: unscoped) 'inScope' accepts.
    [[nodiscard]] auto recreateFollowing(const std::function<bool(std::optional<std::uint32_t>)>& inScope) -> std::expected<void, Error>;
    /// @brief Before recording the frame of slot 'frameIndex': the per-frame buffer accessors pick its copy.
    auto beginFrame(std::uint32_t frameIndex) -> void;
    /// @brief After a frame was recorded: nothing is fresh any more, the history pairs swap, and no frame is recorded.
    auto endFrame() -> void;
    auto bind(ResourceId handle, const renderer::Texture& texture) -> void;
    auto bind(ResourceId handle, rhi::ImageViewHandle view) -> void;
    auto bind(ResourceId handle, rhi::BufferHandle buffer) -> void;
    [[nodiscard]] auto actual(ResourceId handle) const -> std::uint32_t;
    /// @brief Creates, recreates or frees the mip views of the created texture entry 'entry' to match its images' mips.
    [[nodiscard]] auto updateMipViews(std::uint32_t entry) -> std::expected<void, Error>;
    /// @brief Which of a created texture's images is current, or with 'previous' the previous frame's.
    [[nodiscard]] auto imageIndex(const CreatedTexture& created, bool previous) const -> std::size_t;
    /// @brief The image of the created or imported entry 'entry'; of a history pair the current one, or with
    ///        'previous' the previous frame's.
    [[nodiscard]] auto textureAt(std::uint32_t entry, bool previous) const -> const renderer::Texture&;
    /// @brief Whether the created entry 'entry' is a fresh persistent texture or history pair.
    [[nodiscard]] auto isFreshAt(std::uint32_t entry) const -> bool;
    /// @brief The description of the texture entry 'entry' at the current output extent.
    [[nodiscard]] auto textureDesc(std::uint32_t entry, rhi::ImageUsage usage) const -> renderer::Texture::Desc;

    rhi::Device& m_device;
    const ResourceDeclarations& m_declarations;
    rhi::Extent2D m_output;
    std::vector<std::optional<rhi::Extent2D>> m_scopeExtents;                                                            // Per scope: its own extent, or none to follow the output
    std::vector<std::uint32_t> m_targets;                                                                                // Per entry: the entry creating its resource
    std::vector<bool> m_previous;                                                                                        // Per entry: it reads the previous frame's image of a history pair
    std::vector<std::variant<std::monostate, CreatedTexture, CreatedBuffer, ImportedTexture, ImportedBuffer>> m_created; // Per entry
    std::uint32_t m_frame{ 0 };                                                                                          // Frames recorded; its parity picks the current image of a history pair
    std::optional<std::uint32_t> m_frameIndex;                                                                           // The slot of the frame being recorded, between beginFrame and endFrame
};
}
