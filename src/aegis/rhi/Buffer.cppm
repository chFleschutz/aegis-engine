module;
#include <expected>

#include "vma/vma.h"

export module aegis.rhi:buffer;
import :fwd;
import :memory;
import vulkan_hpp;

export namespace aegis::rhi
{
class Buffer
{
    friend Device;

public:
    struct Desc
    {
        std::size_t size;
        vk::BufferUsageFlags usage{ vk::BufferUsageFlagBits::eUniformBuffer }; // TODO: replace with rhi type
        VkMemoryPropertyFlags requiredFlags{ 0 }; //TODO: replace with rhi type
        VkMemoryPropertyFlags preferredFlags{ 0 }; //TODO: replace with rhi type
    };

    Buffer(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    ~Buffer();

    auto operator=(const Buffer&) -> Buffer& = delete;
    auto operator=(Buffer&& other) noexcept -> Buffer&;

private:
    [[nodiscard]] static auto create(
        const Allocator& allocator,
        const Desc& desc)
        -> std::expected<Buffer, Error>;

    Buffer(VmaAllocator allocator, VmaAllocation allocation, vk::Buffer buffer);

    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    vk::Buffer m_buffer;
};
}
