module;
#include <expected>

export module aegis.rhi:sync;
import :error;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class Fence
{
public:
    struct Desc
    {
        const Device& device;
    };

    [[nodiscard]] static auto create(const Desc& desc)
        -> std::expected<Fence, Error>;

    [[nodiscard]] auto operator*() const noexcept
        -> vk::Fence { return *m_fence; }

    [[nodiscard]] auto wait() const noexcept
        -> bool;

    [[nodiscard]] auto reset() const noexcept -> bool;

private:
    explicit Fence(vk::raii::Fence fence);

    vk::raii::Fence m_fence;
};

class Semaphore
{
public:
    struct Desc
    {
        const Device& device;
    };

    [[nodiscard]] static auto create(const Desc& desc)
        -> std::expected<Semaphore, Error>;

    [[nodiscard]] auto operator*() const noexcept
        -> vk::Semaphore { return *m_semaphore; }

private:
    explicit Semaphore(vk::raii::Semaphore semaphore);

    vk::raii::Semaphore m_semaphore;
};
}
