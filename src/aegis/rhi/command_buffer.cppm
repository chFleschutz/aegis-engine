module;
#include <expected>
#include <optional>
#include <span>

export module aegis.rhi:command_buffer;
import :error;
import :commands;
import :fwd;

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

    auto operator*() const
        -> vk::CommandBuffer { return *m_commandBuffer; }

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

    auto transitionImageLayout(const ImageLayoutTransition& cmd) const
        -> void;

private:
    explicit CommandBuffer(vk::raii::CommandBuffer cmdBuffer);

    vk::raii::CommandBuffer m_commandBuffer;
};
}
