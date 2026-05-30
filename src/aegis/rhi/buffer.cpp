module;
#include <vk_mem_alloc.h>

#include <cassert>
#include <expected>
#include <utility>

module aegis.rhi;
import :buffer;
import :error;
import :memory;
import :vulkan_conversions;

namespace aegis::rhi
{
auto Buffer::write(const void* src, std::size_t size, std::size_t offset) const -> void
{
    assert(m_mappedData != nullptr && "Cannot write to unmapped buffer");
    assert(src != nullptr && "Cannot copy from nullptr");

    std::memcpy(static_cast<std::byte*>(m_mappedData) + offset, src, size);

    if (!m_isCoherent)
        m_allocation.flush(offset, size);
}

auto Buffer::read(void* dst, std::size_t size, std::size_t offset) const -> void
{
    assert(m_mappedData != nullptr && "Cannot read from unmapped buffer");
    assert(dst != nullptr && "Cannot copy to nullptr");

    if (!m_isCoherent)
        m_allocation.invalidate(offset, size);

    std::memcpy(dst, static_cast<std::byte*>(m_mappedData) + offset, size);
}

auto Buffer::create(const Allocator& allocator, const Desc& desc) -> std::expected<Buffer, Error>
{
    vk::BufferCreateInfo bufferInfo{
        .size = static_cast<vk::DeviceSize>(desc.size),
        .usage = toVulkan(desc.usage),
        .sharingMode = vk::SharingMode::eExclusive,
    };

    auto bufferAlloc = allocator.allocateBuffer(bufferInfo, desc.memory);
    if (!bufferAlloc)
        return std::unexpected{ bufferAlloc.error() };

    return Buffer{
        std::move(bufferAlloc->first),
        vk::DeviceSize{ bufferAlloc->second.size },
        bufferAlloc->second.pMappedData
    };
}

Buffer::Buffer(
    BufferAllocation allocation,
    vk::DeviceSize size,
    void* mappedData) :
    m_allocation{ std::move(allocation) },
    m_size{ size },
    m_mappedData{ mappedData }
{
    auto memoryFlags = m_allocation.queryMemoryProperties();
    m_isCoherent = static_cast<bool>(memoryFlags & vk::MemoryPropertyFlagBits::eHostCoherent);
}
}
