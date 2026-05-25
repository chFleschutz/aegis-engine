module;
#include "vma/vma.h"

#include <expected>

export module aegis.rhi:memory;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class Allocator
{
    friend Device;

public:
    Allocator(const Allocator&) = delete;
    Allocator(Allocator&& other) noexcept;
    ~Allocator();

    auto operator=(const Allocator&) -> Allocator& = delete;
    auto operator=(Allocator&& other) noexcept -> Allocator&;

    auto operator*() const noexcept -> VmaAllocator { return m_allocator; }

private:
    [[nodiscard]] static auto create(
        const vk::raii::Instance& instance,
        const vk::raii::Device& device,
        const vk::raii::PhysicalDevice& physicalDevice)
        -> std::expected<Allocator, Error>;

    explicit Allocator(VmaAllocator allocator);

    VmaAllocator m_allocator;
};
}
