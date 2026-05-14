module;
#include <expected>

export module aegis.rhi:command_buffer;
import :command_pool;
import :error;

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

    [[nodiscard]] static auto create(const Desc& desc)
        -> std::expected<CommandBuffer, Error>;

private:
    explicit CommandBuffer(vk::raii::CommandBuffer cmdBuffer);

    vk::raii::CommandBuffer m_commandBuffer;
};
}
