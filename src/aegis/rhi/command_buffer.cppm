module;
#include <expected>
#include <optional>
#include <span>
#include <string_view>

export module aegis.rhi:command_buffer;
import :error;
import :commands;
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

    [[nodiscard]] auto operator*() const noexcept -> vk::CommandBuffer { return *m_commandBuffer; }
    [[nodiscard]] auto handle() const noexcept -> vk::CommandBuffer { return *m_commandBuffer; }

    auto begin(bool oneTimeSubmit = false) const -> void;
    auto end() const -> void;

    auto beginRendering(const RenderingCmd& desc) const -> void;
    auto endRendering() const -> void;

    auto beginLabel(std::string_view name, std::array<float, 4> color = {}) const -> void;
    auto endLabel() const -> void;
    auto insertLabel(std::string_view name, std::array<float, 4> color = {}) const -> void;

    auto bindPipeline(const Pipeline& pipeline) const -> void;
    auto setViewport(Extent2D extent) const -> void;
    auto setScissor(Extent2D extent) const -> void;
    auto draw(std::uint32_t vertexCount) const -> void;

    auto transitionImageLayout(ImageViewHandle imageViewHandle, ResourceState oldState,
        ResourceState newState) const -> void;
    auto transitionImageLayout(ImageHandle imageHandle, ResourceState oldState,
        ResourceState newState) const -> void;
    auto generateMipmaps(ImageHandle imageHandle, ResourceState currentState) const -> void;

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
};
}
