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
    friend class Device;

public:
    struct Desc
    {
        bool signaled{ true };
    };

    [[nodiscard]] auto operator*() const noexcept -> vk::Fence { return *m_fence; }

    [[nodiscard]] auto wait() const noexcept -> bool;
    [[nodiscard]] auto reset() const noexcept -> bool;

private:
    [[nodiscard]] static auto create(
        const Device& device,
        const Desc& desc)
        -> std::expected<Fence, Error>;

    explicit Fence(vk::raii::Fence fence);

    vk::raii::Fence m_fence;
};

class Semaphore
{
    friend class Device;

public:
    enum class Type
    {
        Binary,
        Timeline
    };

    struct Desc
    {
        Type type{ Type::Binary };
    };

    [[nodiscard]] auto operator*() const noexcept -> vk::Semaphore { return *m_semaphore; }
    [[nodiscard]] auto operator->() const noexcept -> const vk::raii::Semaphore* { return &m_semaphore; }

private:
    [[nodiscard]] static auto create(
        const vk::raii::Device& device,
        const Desc& desc)
        -> std::expected<Semaphore, Error>;

    explicit Semaphore(vk::raii::Semaphore semaphore);

    vk::raii::Semaphore m_semaphore;
};
}
