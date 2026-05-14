module;
#include <expected>
#include <optional>
#include <span>

export module aegis.rhi:command_buffer;
import :command_pool;
import :error;
import :pipeline;

export namespace aegis::rhi
{
class CommandBuffer
{
public:
    struct Desc
    {
        const Device& device;
        const CommandPool& pool;
    };

    struct AttachmentDesc
    {
        // TODO: Dont use vulkan types
        vk::ImageView imageView;
        vk::ImageLayout imageLayout;
        vk::ClearValue clearValue;
    };

    struct RenderingDesc
    {
        std::pair<std::uint32_t, std::uint32_t> extent;
        std::span<AttachmentDesc> attachments;
        // TODO: Add depth attachment
    };

    [[nodiscard]] static auto create(const Desc& desc)
        -> std::expected<CommandBuffer, Error>;

    auto begin() const
        -> void;

    auto end() const
        -> void;

    auto beginRendering(const RenderingDesc& desc) const
        -> void;

    auto endRendering() const
        -> void;

    auto bindPipeline(const Pipeline& pipeline) const
        -> void;

    auto setViewport(std::uint32_t width,
        std::uint32_t height) const
        -> void;

    auto setScissor(std::uint32_t width,
        std::uint32_t height) const
        -> void;

    auto draw(std::uint32_t vertexCount) const
        -> void;

private:
    explicit CommandBuffer(vk::raii::CommandBuffer cmdBuffer);

    vk::raii::CommandBuffer m_commandBuffer;
};
}
