module;
#include <expected>
#include <optional>

export module aegis.rhi:command_buffer;
import :error;
import :commands;
import :fwd;
import vulkan_hpp;

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

    static constexpr std::uint32_t maxColorAttachments = 8;

    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<CommandBuffer, Error>;

    explicit CommandBuffer(vk::raii::CommandBuffer cmdBuffer);

    auto operator*() const -> vk::CommandBuffer { return *m_commandBuffer; }

    auto begin() const -> void;
    auto end() const -> void;

    auto beginRendering(const RenderingCmd& desc) const -> void;
    auto endRendering() const -> void;

    auto bindPipeline(const Pipeline& pipeline) const -> void;
    auto setViewport(Extent2D extent) const -> void;
    auto setScissor(Extent2D extent) const -> void;
    auto draw(std::uint32_t vertexCount) const -> void;

    auto transitionImageLayout(const ImageLayoutTransition& cmd) const -> void;

private:
    static auto deriveExtent(const RenderingCmd& cmd) -> vk::Extent2D;

    vk::raii::CommandBuffer m_commandBuffer;
};
}
