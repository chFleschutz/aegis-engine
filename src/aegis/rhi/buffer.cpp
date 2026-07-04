module;
#include <vk_mem_alloc.h>

#include <cassert>
#include <expected>
#include <utility>

module aegis.rhi;
import :buffer;
import :debug;
import :error;
import :memory;
import :vulkan_conversions;

namespace aegis::rhi
{
auto Buffer::write(const std::byte* src, std::size_t size, std::size_t offset) const -> void
{
    assert(m_mappedData != nullptr && "Cannot write to unmapped buffer");
    assert(src != nullptr && "Cannot copy from nullptr");

    std::memcpy(static_cast<std::byte*>(m_mappedData) + offset, src, size);

    if (!(m_memoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent))
        m_allocation.flush(offset, size);
}

auto Buffer::read(std::byte* dst, std::size_t size, std::size_t offset) const -> void
{
    assert(m_mappedData != nullptr && "Cannot read from unmapped buffer");
    assert(dst != nullptr && "Cannot copy to nullptr");

    if (!(m_memoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent))
        m_allocation.invalidate(offset, size);

    std::memcpy(dst, static_cast<std::byte*>(m_mappedData) + offset, size);
}

auto Buffer::create(const Device& device, const Desc& desc) -> std::expected<Buffer, Error>
{
    vk::BufferCreateInfo bufferInfo{
        .size = static_cast<vk::DeviceSize>(desc.size),
        .usage = toVulkan(desc.usage),
        .sharingMode = vk::SharingMode::eExclusive,
    };

    auto bufferAlloc = device.allocator().allocateBuffer(bufferInfo, desc.memory);
    if (!bufferAlloc)
        return std::unexpected{ bufferAlloc.error() };

    debug::setName(*device, *bufferAlloc->first, desc.name);

    return Buffer{
        std::move(bufferAlloc->first),
        vk::DeviceSize{ bufferAlloc->second.size },
        static_cast<std::byte*>(bufferAlloc->second.pMappedData)
    };
}

Buffer::Buffer(
    BufferAllocation allocation,
    vk::DeviceSize size,
    std::byte* mappedData) :
    m_allocation{ std::move(allocation) },
    m_size{ size },
    m_mappedData{ mappedData },
    m_memoryFlags{ m_allocation.queryMemoryProperties() }
{
}
}
