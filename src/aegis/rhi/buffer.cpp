module;
#include "vma/vma.h"

#include <utility>
#include <expected>

module aegis.rhi;
import :buffer;
import :error;
import :memory;
import :vulkan_conversions;

namespace aegis::rhi
{
Buffer::Buffer(Buffer&& other) noexcept :
    m_allocator{ other.m_allocator },
    m_allocation{ std::exchange(other.m_allocation, nullptr) },
    m_buffer{ std::exchange(other.m_buffer, nullptr) }
{
}

Buffer::~Buffer()
{
    if (m_buffer)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
}

auto Buffer::operator=(Buffer&& other) noexcept -> Buffer&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
        std::swap(m_allocation, other.m_allocation);
        std::swap(m_buffer, other.m_buffer);
    }
    return *this;
}

auto Buffer::create(const Allocator& allocator, const Desc& desc) -> std::expected<Buffer, Error>
{
    vk::BufferCreateInfo bufferInfo{
        .size = static_cast<vk::DeviceSize>(desc.size),
        .usage = desc.usage,
        .sharingMode = vk::SharingMode::eExclusive,
    };

    VmaAllocationCreateInfo allocationInfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = desc.requiredFlags,
        .preferredFlags = desc.preferredFlags,
    };

    VkBuffer buffer{ nullptr };
    VmaAllocation allocation{ nullptr };
    auto result = vk::Result{
        vmaCreateBuffer(*allocator,
            &static_cast<const VkBufferCreateInfo&>(bufferInfo),
            &allocationInfo,
            &buffer,
            &allocation,
            nullptr)
    };

    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return Buffer{
        *allocator,
        allocation,
        vk::Buffer{ buffer },
    };
}

Buffer::Buffer(VmaAllocator allocator, VmaAllocation allocation, vk::Buffer buffer) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_buffer{ buffer }
{
}
}
