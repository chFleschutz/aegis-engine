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

private:
    [[nodiscard]] static auto create(
        const vk::raii::Instance& instance,
        const vk::raii::Device& device,
        const vk::raii::PhysicalDevice& physicalDevice)
        -> std::expected<Allocator, Error>;

    explicit Allocator(VmaAllocator allocator);

    VmaAllocator m_allocator;
};


class Allocation
{
public:
    struct Desc
    {
    };

    Allocation(const Allocation&) = delete;
    Allocation(Allocation&& other) noexcept;
    ~Allocation();

    auto operator=(const Allocation&) -> Allocation& = delete;
    auto operator=(Allocation&& other) noexcept -> Allocation&;

    auto map() const -> std::expected<void*, Error>;
    auto unmap() const -> void;

private:
    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<Allocation, Error>;

    Allocation(VmaAllocator allocator, VmaAllocation allocation);

    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
};
}
