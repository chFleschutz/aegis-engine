module;
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

export module aegis.rhi:command_buffer;
import :error;
import :commands;
import :common;
import :fwd;
import :resource_handle;
import vulkan_hpp;

export namespace aegis::rhi
{
class CommandBuffer
{
    friend class Device;

public:
    struct Desc
    {
        std::string_view name;
        const CommandPool& pool;
    };

    static constexpr std::uint32_t maxColorAttachments = 8;
    /// @brief Size of the push-constant block every pipeline shares; the minimum Vulkan guarantees.
    static constexpr std::uint32_t maxPushConstantSize = 128;

    [[nodiscard]] auto operator*() const noexcept -> vk::CommandBuffer { return *m_commandBuffer; }
    [[nodiscard]] auto handle() const noexcept -> vk::CommandBuffer { return *m_commandBuffer; }

    auto begin(bool oneTimeSubmit = false) const -> void;
    auto end() const -> void;

    auto beginRendering(const RenderingCmd& desc) const -> void;
    auto endRendering() const -> void;

    auto beginLabel(std::string_view name, std::array<float, 4> color = {}) const -> void;
    auto endLabel() const -> void;
    auto insertLabel(std::string_view name, std::array<float, 4> color = {}) const -> void;

    /// @brief Also binds the bindless set, the first time a pipeline of its bind point is bound.
    auto bindPipeline(const Pipeline& pipeline) const -> void;
    auto setViewport(Extent2D extent) const -> void;
    auto setScissor(Extent2D extent) const -> void;
    auto draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0,
        std::uint32_t firstInstance = 0) const -> void;
    /// @brief Binds 'buffer' (32-bit indices, created with BufferUsage::Index) for drawIndexed.
    auto bindIndexBuffer(const Buffer& buffer, std::size_t offset = 0) const -> void;
    auto drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0,
        std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) const -> void;
    /// @brief Mesh-shader draws read from 'commands' (12-byte VkDrawMeshTasksIndirectCommandEXT each),
    ///        as many as the 32-bit count at 'countOffset' in 'counts' says, at most 'maxDrawCount'.
    /// @note Needs Device::capabilities().meshShaders; both buffers need BufferUsage::Indirect.
    auto drawMeshTasksIndirectCount(const Buffer& commands, std::size_t offset, const Buffer& counts,
        std::size_t countOffset, std::uint32_t maxDrawCount) const -> void;
    auto dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY = 1, std::uint32_t groupCountZ = 1) const
        -> void;

    /// @brief Writes 'data' into the shared push-constant block at 'offset', visible to all stages.
    /// @note Offset and size must be multiples of 4, and offset + size at most maxPushConstantSize.
    auto pushConstants(std::span<const std::byte> data, std::uint32_t offset = 0) const -> void;

    template<typename T>
        requires std::is_trivially_copyable_v<T> && (!std::convertible_to<const T&, std::span<const std::byte>>)
    auto pushConstants(const T& value, std::uint32_t offset = 0) const -> void
    {
        static_assert(sizeof(T) <= maxPushConstantSize, "Push constants are limited to 128 bytes");
        pushConstants(std::as_bytes(std::span{ &value, 1 }), offset);
    }

    auto transitionImageLayout(ImageViewHandle imageViewHandle, ResourceState oldState,
        ResourceState newState) const -> void;
    auto transitionImageLayout(ImageHandle imageHandle, ResourceState oldState,
        ResourceState newState) const -> void;
    /// @brief Makes every earlier copy and blit write available and visible to all later commands.
    ///        Buffers have no layout, so this is the only dependency an uploaded buffer needs.
    auto makeTransferWritesVisible() const -> void;
    /// @brief Orders all memory accesses of state 'before' against later ones of state 'after'. Image
    ///        layouts are ignored; this is the dependency a buffer, which has no layout, needs.
    auto memoryBarrier(ResourceState before, ResourceState after) const -> void;

    auto generateMipmaps(ImageHandle imageHandle, ResourceState currentState) const -> void;

    auto resetQueries(const QueryPool& pool, std::uint32_t first, std::uint32_t count) const -> void;
    auto writeTimestamp(const QueryPool& pool, std::uint32_t query, TimestampStage stage) const -> void;

    /// @brief Scales the first mip and layers of view 'src' onto those of view 'dst', converting formats.
    /// @note 'src' must be in ResourceState::CopySrc and 'dst' in ResourceState::CopyDst.
    auto blit(ImageViewHandle src, ImageViewHandle dst, Filter filter = Filter::Linear) const -> void;

    /// @brief Fills 'size' bytes of 'buffer' at 'offset' (both multiples of 4) with the 32-bit 'value'.
    ///        Needs BufferUsage::TransferDst; counts as a transfer write (ResourceState::CopyDst).
    auto fillBuffer(const Buffer& buffer, std::uint32_t value, std::size_t offset, std::size_t size) const -> void;

    auto copyBuffer(const Buffer& src, const Buffer& dst, std::size_t size, std::size_t srcOffset = 0,
        std::size_t dstOffset = 0) const -> void;

    /// @brief Copies bytes from buffer 'src' into image 'dst'.
    /// @note Image 'dst' must already be in ResourceState::CopyDst.
    auto copyBufferToImage(const Buffer& src, ImageHandle dst,
        std::span<const BufferImageCopy> regions) const -> void;

    /// @brief Copies bytes from image 'src' into buffer 'dst'.
    /// @note Image 'src' must already be in ResourceState::CopySrc.
    auto copyImageToBuffer(ImageHandle src, const Buffer& dst,
        std::span<const BufferImageCopy> regions) const -> void;

private:
    [[nodiscard]] static auto create(const Device& device, const Desc& desc)
        -> std::expected<CommandBuffer, Error>;

    CommandBuffer(const Device& device, vk::raii::CommandBuffer cmdBuffer);

    auto deriveExtent(const RenderingCmd& cmd) const -> vk::Extent2D;

    const Device& m_device;
    vk::raii::CommandBuffer m_commandBuffer;
    // Whether the bindless set is bound for each bind point in the current recording; begin() resets.
    mutable bool m_graphicsSetBound{ false };
    mutable bool m_computeSetBound{ false };
};
}
