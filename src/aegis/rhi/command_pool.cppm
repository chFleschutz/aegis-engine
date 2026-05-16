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
public:
    struct Desc
    {
        const class Device& device;
        std::uint32_t queueFamily;
    };

    static auto create(const Desc& desc)
        -> std::expected<CommandPool, Error>;

    [[nodiscard]] auto commandPool() const
        -> const vk::raii::CommandPool& { return m_commandPool; }

private:
    explicit CommandPool(vk::raii::CommandPool pool);

    vk::raii::CommandPool m_commandPool;
};
}
