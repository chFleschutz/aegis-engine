export module aegis.rhi:command_pool;
import std;
import :error;
import :fwd;
import vulkan;

export namespace aegis::rhi
{
class CommandPool
{
    friend class Device;

public:
    struct Desc
    {
        std::string_view name;
        std::uint32_t queueFamily;
    };

    [[nodiscard]] auto operator->() const noexcept -> const vk::raii::CommandPool& { return m_commandPool; };
    [[nodiscard]] auto operator*() const noexcept -> vk::CommandPool { return *m_commandPool; }
    [[nodiscard]] auto handle() const noexcept -> vk::CommandPool { return *m_commandPool; }

private:
    [[nodiscard]] static auto create(const Device& device, const Desc& desc)
        -> std::expected<CommandPool, Error>;

    explicit CommandPool(vk::raii::CommandPool pool);

    vk::raii::CommandPool m_commandPool;
};
}
