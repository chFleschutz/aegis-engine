module;
#include "vma/vma.h"

#include <utility>
#include <expected>
#include <cassert>

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

auto Buffer::write(const void* src, std::size_t size, std::size_t offset) const -> void
{
    assert(m_mappedData != nullptr && "Cannot write to unmapped buffer");
    assert(src != nullptr && "Cannot copy from nullptr");

    std::memcpy(static_cast<std::byte*>(m_mappedData) + offset, src, size);

    if (!m_isCoherent)
        vmaFlushAllocation(m_allocator, m_allocation, offset, size);
}

auto Buffer::read(void* dst, std::size_t size, std::size_t offset) const -> void
{
    assert(m_mappedData != nullptr && "Cannot read from unmapped buffer");
    assert(dst != nullptr && "Cannot copy to nullptr");

    if (!m_isCoherent)
        vmaInvalidateAllocation(m_allocator, m_allocation, offset, size);

    std::memcpy(dst, static_cast<std::byte*>(m_mappedData) + offset, size);
}

auto Buffer::create(const Allocator& allocator, const Desc& desc) -> std::expected<Buffer, Error>
{
    auto usage = deriveBufferFlags(desc.usage);

    vk::BufferCreateInfo bufferInfo{
        .size = static_cast<vk::DeviceSize>(desc.size),
        .usage = usage.bufferUsage,
        .sharingMode = vk::SharingMode::eExclusive,
    };

    VmaAllocationCreateInfo allocationInfo{
        .usage = usage.memoryUsage,
        .requiredFlags = static_cast<VkMemoryPropertyFlags>(usage.requiredFlags),
        .preferredFlags = static_cast<VkMemoryPropertyFlags>(usage.preferredFlags),
    };

    VkBuffer buffer{ nullptr };
    VmaAllocation allocation{ nullptr };
    VmaAllocationInfo allocInfo{};
    auto result = vk::Result{
        vmaCreateBuffer(*allocator,
            &static_cast<const VkBufferCreateInfo&>(bufferInfo),
            &allocationInfo,
            &buffer,
            &allocation,
            &allocInfo)
    };

    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return Buffer{
        *allocator,
        allocation,
        vk::Buffer{ buffer },
        vk::DeviceSize{ allocInfo.size },
        allocInfo.pMappedData
    };
}

Buffer::Buffer(VmaAllocator allocator,
    VmaAllocation allocation,
    vk::Buffer buffer,
    vk::DeviceSize size,
    void* mappedData) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_buffer{ buffer },
    m_size{ size },
    m_mappedData{ mappedData }
{
    VkMemoryPropertyFlags memoryFlags;
    vmaGetAllocationMemoryProperties(m_allocator, m_allocation, &memoryFlags);
    m_isCoherent = (memoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
}
}
