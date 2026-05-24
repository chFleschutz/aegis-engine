module;
#include <expected>

export module aegis.rhi:command_pool;
import :error;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class CommandPool
{
    friend class Device;

public:
    struct Desc
    {
        std::uint32_t queueFamily;
    };

    [[nodiscard]] auto pool() const -> const vk::raii::CommandPool& { return m_commandPool; }

private:
    [[nodiscard]] static auto create(
        const Device& device,
        const Desc& desc)
        -> std::expected<CommandPool, Error>;

    explicit CommandPool(vk::raii::CommandPool pool);

    vk::raii::CommandPool m_commandPool;
};
}
